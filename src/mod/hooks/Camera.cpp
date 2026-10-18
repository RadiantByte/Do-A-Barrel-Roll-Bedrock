// Camera orientation / roll.
//
// While flying, the camera orientation is built from the controller's own (pitch, yaw, roll) triple instead of the
// player's rotation. The triple stays continuous when the look passes vertical (yaw and roll flip by 180 together, they
// describe the same orientation), so loops and barrel rolls have no discontinuity regardless of how the game clamps,
// wraps or interpolates the player's rotation. When not flying (roll-back decay) the game's own orientation is simply
// rolled about the view axis.
//
// WHERE THE ROLL IS INJECTED (disassembly of Minecraft.Windows.exe 1.26.51, RVAs):
//   LevelRendererPlayer::setupCamera (RVA 0x46cfe60) is the only producer of the render camera. It does NOT derive the
//   view from the player. It fetches the ECS entity tagged MinecraftCamera::RenderCameraComponent (entt type hash
//   0x119e772b), reads that entity's MinecraftCamera::CameraComponent (hash 0x4f6047c7, 288 byte component, quaternion
//   mOrientation at +0x30) and builds from it, in one go:
//     - ScreenContext.camera.viewMatrixStack top  (= mPostViewTransform * inverse(mat4_cast(mOrientation)))
//     - the projection top, the world matrix top
//     - snapshots of view / projection inside the LevelRendererPlayer object (+0xf18 / +0xf58), camera position
//       (+0x654..), the listener forward/up vectors (+0x11e8..) and fog data.
//   Everything the renderer later uses (terrain, sky, clouds, entities, the culling frustum made by _prepareCuller from
//   the camera's matrix stacks, _saveMatrices) is derived from those, and setupCamera runs twice per frame
//   (InGamePlayScreen::_renderLevelPrep at RVA 0x4ba840 and InGamePlayScreen::render at RVA 0x4bdfb0, both with
//   ScreenContext.camera = [ScreenContext + 0x18]). Editing the finished matrix stack after the fact (the old approach)
//   missed the snapshots and the derived vectors, hence the inconsistent partial rolls.
//   So the roll is injected into CameraComponent::mOrientation BEFORE setupCamera runs; the game then derives every
//   matrix, snapshot and vector from the rolled orientation consistently. The original orientation is restored when
//   the render scope (_renderLevelPrep / render) ends, so ECS systems never see our roll.
//
// Fallback (only if the camera component cannot be found): the old post-origin edit of the camera's view matrix.

#include "mod/hooks/State.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/InGamePlayScreen.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/minecraft_camera/components/CameraComponent.h"
#include "mc/deps/minecraft_camera/components/RenderCameraComponent.h"
#include "mc/deps/renderer/Camera.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cmath>
#include <cstring>

// The mod's entt must compute the same component type ids as the game (they are FNV-1a of the type name).
static_assert(
    entt::type_hash<MinecraftCamera::CameraComponent>::value() == 0x4f6047c7u,
    "entt type hash of CameraComponent differs from the game's"
);
static_assert(
    entt::type_hash<MinecraftCamera::RenderCameraComponent>::value() == 0x119e772bu,
    "entt type hash of RenderCameraComponent differs from the game's"
);

namespace do_a_barrel_roll::hooks {

namespace {

using Clock = std::chrono::steady_clock;

struct Basis {
    glm::dvec3 r, u, f;
};

glm::dvec3 rotateAxis(glm::dvec3 v, glm::dvec3 k, double a) {
    double c = std::cos(a), s = std::sin(a);
    return v * c + glm::cross(k, v) * s + k * (glm::dot(k, v) * (1.0 - c));
}

// World basis of an orientation: facing, right (physical right wing at roll 0) and up. Positive roll = right wing down
// (rotation of right/up about facing, right-handed), same convention as the core.
Basis makeBasis(double pitchDeg, double yawDeg, double rollDeg) {
    double p = glm::radians(pitchDeg), y = glm::radians(yawDeg);
    glm::dvec3 f{-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
    glm::dvec3 r{-std::cos(y), 0.0, -std::sin(y)};
    glm::dvec3 u = glm::cross(r, f);
    double     a = glm::radians(rollDeg);
    if (a != 0.0) {
        r = rotateAxis(r, f, a);
        u = rotateAxis(u, f, a);
    }
    return {r, u, f};
}

Basis rollBasis(Basis b, double rollDeg) {
    double a = glm::radians(rollDeg);
    b.r      = rotateAxis(b.r, b.f, a);
    b.u      = rotateAxis(b.u, b.f, a);
    return b;
}

glm::dvec3 row(glm::mat4 const& m, int i) { return {m[0][i], m[1][i], m[2][i]}; }

// Roll as it ends up in a view matrix (rows = right, up, -forward): right wing down => right.y < 0.
double measureRoll(glm::mat4 const& view) {
    glm::dvec3 r1 = row(view, 0), u1 = row(view, 1);
    return glm::degrees(std::atan2(-r1.y, u1.y));
}

glm::mat4& viewOf(mce::Camera& camera) { return *camera.viewMatrixStack->getTop()._m; }

// ---- ECS camera component -------------------------------------------------------------------------------------------

using Registry = entt::basic_registry<EntityId>;

struct Pending {
    bool         active = false;
    Registry*    registry = nullptr;
    EntityId     entity{};
    glm::quat    original{};
};
Pending pending;

// The camera component of the entity tagged RenderCameraComponent, exactly what setupCamera reads.
MinecraftCamera::CameraComponent* findRenderCamera(Registry*& registryOut, EntityId& entityOut) {
    auto client = ll::service::getClientInstance();
    if (!client) return nullptr;
    auto* player = client->getLocalPlayer();
    if (!player) return nullptr;
    Registry&       reg   = player->getEntityContext().getRegistry();
    Registry const& creg  = reg;
    auto const*     tags  = creg.storage<MinecraftCamera::RenderCameraComponent>();
    if (!tags) return nullptr;
    for (auto it = tags->begin(); it != tags->end(); ++it) {
        EntityId e = *it;
        if (auto const* cc = creg.try_get<MinecraftCamera::CameraComponent>(e)) {
            registryOut = &reg;
            entityOut   = e;
            return const_cast<MinecraftCamera::CameraComponent*>(cc);
        }
    }
    return nullptr;
}

void restoreOrientation(char const* where) {
    if (!pending.active) return;
    pending.active = false;
    if (auto* cc = pending.registry->try_get<MinecraftCamera::CameraComponent>(pending.entity)) {
        cc->mOrientation.get() = pending.original;
    }
    (void)where;
}

Clock::time_point lastLog{};
int               callsSinceLog = 0;
bool              loggedHash    = false;
bool              loggedFallback = false;

// Wanted camera basis given the game's current one.
Basis wantedBasis(Basis const& current, bool override_, float roll) {
    return override_ ? makeBasis(state::cameraPitch, state::cameraYaw, roll) : rollBasis(current, roll);
}

// Returns true if the component's orientation was replaced (pending restore armed).
bool injectIntoComponent(MinecraftCamera::CameraComponent& cc, Registry* reg, EntityId entity, char const*& result) {
    glm::quat q0 = cc.mOrientation.get();
    glm::mat3 r0 = glm::mat3_cast(q0);
    Basis     b0{glm::dvec3(r0[0]), glm::dvec3(r0[1]), -glm::dvec3(r0[2])};

    bool const override_ = state::cameraOverride;
    float const roll     = state::cameraRollDeg;
    Basis       t        = wantedBasis(b0, override_, roll);

    glm::mat3 m{glm::vec3(t.r), glm::vec3(t.u), glm::vec3(-t.f)};
    glm::quat q1 = glm::normalize(glm::quat_cast(m));
    if (!std::isfinite(q1.x) || !std::isfinite(q1.y) || !std::isfinite(q1.z) || !std::isfinite(q1.w)) {
        result = "bad quaternion";
        return false;
    }
    // Keep the same hemisphere as the game's quaternion (q and -q are the same rotation, this only avoids sign flips).
    if (glm::dot(q0, q1) < 0.0f) q1 = -q1;

    pending.active   = true;
    pending.registry = reg;
    pending.entity   = entity;
    pending.original = q0;
    cc.mOrientation.get() = q1;
    result                = "ecs";
    return true;
}

// ---- fallback: edit the finished view matrix -------------------------------------------------------------------------

bool applyMatrixFallback(mce::Camera& camera) {
    bool const override_ = state::cameraOverride;
    float const roll     = state::cameraRollDeg;
    glm::mat4& view      = viewOf(camera);

    glm::dvec3 rr = row(view, 0), uu = row(view, 1), ff = -row(view, 2);
    Basis      b0{rr, uu, ff};
    if (auto client = ll::service::getClientInstance(); client) {
        if (auto* player = client->getLocalPlayer()) {
            auto const& rot = player->getRotation();
            Basis       pb  = makeBasis(rot.x, rot.y, 0.0);
            if (glm::dot(ff, pb.f) < 0.7 || glm::dot(rr, pb.r) < 0.7) b0 = pb;
        }
    }
    Basis     target = wantedBasis(b0, override_, roll);
    glm::mat3 w      = glm::outerProduct(b0.r, target.r) + glm::outerProduct(b0.u, target.u)
                + glm::outerProduct(b0.f, target.f);
    glm::mat4 rot4 = glm::mat4(glm::mat3(w));
    glm::vec3 eye  = -(glm::transpose(glm::mat3(view)) * glm::vec3(view[3]));
    glm::mat4 res  = view * glm::translate(glm::mat4(1.0f), eye) * rot4 * glm::translate(glm::mat4(1.0f), -eye);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(res[c][r])) return false;
    view = res;
    return true;
}

LL_TYPE_INSTANCE_HOOK(
    SetupCameraHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::setupCamera,
    void,
    mce::Camera& camera,
    float        a
) {
    // A previous scope that did not get closed (e.g. an exception path): never stack our roll on itself.
    restoreOrientation("setupCamera-start");

    bool const wanted = state::cameraOverride || state::cameraRollDeg != 0.0f;
    ++callsSinceLog;
    auto now = Clock::now();

    if (!wanted) {
        origin(camera, a);
        return;
    }

    if (state::config.debugLogging && !loggedHash) {
        loggedHash = true;
        state::debugLog(
            "camera: entt type_hash CameraComponent={:#x} (game 0x4f6047c7) RenderCameraComponent={:#x} (game 0x119e772b)",
            static_cast<unsigned>(entt::type_hash<MinecraftCamera::CameraComponent>::value()),
            static_cast<unsigned>(entt::type_hash<MinecraftCamera::RenderCameraComponent>::value())
        );
    }

    Registry* reg = nullptr;
    EntityId  ent{};
    auto*     cc = findRenderCamera(reg, ent);

    char const* path = "none";
    glm::quat   qBefore{}, qInjected{};
    if (cc) {
        qBefore = cc->mOrientation.get();
        if (injectIntoComponent(*cc, reg, ent, path)) qInjected = cc->mOrientation.get();
    }

    origin(camera, a);

    bool usedFallback = false;
    if (std::strcmp(path, "ecs") != 0) {
        usedFallback = true;
        if (applyMatrixFallback(camera)) {
            camera.updateViewMatrixDependencies();
            path = "matrix-fallback";
        }
        if (state::config.debugLogging && !loggedFallback) {
            loggedFallback = true;
            state::debugLog("camera: ECS render camera component NOT found, using the post-origin matrix fallback");
        }
    }
    (void)usedFallback;

    if (state::config.debugLogging && now - lastLog > std::chrono::seconds(1)) {
        lastLog = now;
        glm::mat4 const& view = *camera.viewMatrixStack->getTop()._m;
        // Snapshot setupCamera keeps inside the LevelRendererPlayer (disassembly: +0xf18 view, +0xf58 projection).
        glm::mat4 snap;
        std::memcpy(&snap, reinterpret_cast<char const*>(this) + 0xf18, sizeof(snap));
        bool const snapEqualsView = std::memcmp(&snap, &view, sizeof(snap)) == 0;
        state::debugLog(
            "camera[setupCamera] path={} calls_since_last_log={} cam={} override={} controllerRoll={:.2f} appliedRoll={:.2f} "
            "pitch={:.2f} yaw={:.2f} | viewTopMeasuredRoll={:.2f} lrpSnapshotMeasuredRoll={:.2f} snapshotEqualsView={} | "
            "q_game=({:.3f},{:.3f},{:.3f},{:.3f}) q_injected=({:.3f},{:.3f},{:.3f},{:.3f})",
            path, callsSinceLog, static_cast<void*>(&camera), state::cameraOverride, state::controller.getRoll(),
            state::cameraRollDeg, state::cameraPitch, state::cameraYaw, measureRoll(view), measureRoll(snap),
            snapEqualsView, qBefore.x, qBefore.y, qBefore.z, qBefore.w, qInjected.x, qInjected.y, qInjected.z,
            qInjected.w
        );
        callsSinceLog = 0;
    }
}

// Close the render scopes: the ECS camera gets its own orientation back once the frame part that consumed it is done.
LL_TYPE_INSTANCE_HOOK(
    RenderLevelPrepHook,
    ll::memory::HookPriority::Normal,
    InGamePlayScreen,
    &InGamePlayScreen::$_renderLevelPrep,
    void,
    ScreenContext& screenContext,
    LevelRenderer& levelRenderer,
    Actor&         cameraEntity
) {
    origin(screenContext, levelRenderer, cameraEntity);
    restoreOrientation("renderLevelPrep-end");
}

LL_TYPE_INSTANCE_HOOK(
    ScreenRenderHook,
    ll::memory::HookPriority::Normal,
    InGamePlayScreen,
    &InGamePlayScreen::$render,
    void,
    ScreenContext&           screenContext,
    FrameRenderObject const& renderObj
) {
    origin(screenContext, renderObj);
    restoreOrientation("render-end");
}

// Log only (no modification): the culling frustum is built from exactly this camera's matrix stacks. Proves in the
// debug log that it sees the rolled view and that it is the same camera object setupCamera filled.
LL_TYPE_INSTANCE_HOOK(
    PrepareCullerHook,
    ll::memory::HookPriority::Normal,
    InGamePlayScreen,
    &InGamePlayScreen::$_prepareCuller,
    void,
    mce::Camera& camera,
    Frustum&     frustumData
) {
    static Clock::time_point last{};
    auto                     now = Clock::now();
    if (state::config.debugLogging && (state::cameraOverride || state::cameraRollDeg != 0.0f)
        && now - last > std::chrono::seconds(1)) {
        last = now;
        state::debugLog(
            "camera[_prepareCuller] cam={} viewTopMeasuredRoll={:.2f} pendingInjection={}", static_cast<void*>(&camera),
            measureRoll(*camera.viewMatrixStack->getTop()._m), pending.active
        );
    }
    origin(camera, frustumData);
}

} // namespace

void installCamera() {
    pending = {};
    loggedHash = loggedFallback = false;
    SetupCameraHook::hook();
    RenderLevelPrepHook::hook();
    ScreenRenderHook::hook();
    PrepareCullerHook::hook();
}
void uninstallCamera() {
    PrepareCullerHook::unhook();
    ScreenRenderHook::unhook();
    RenderLevelPrepHook::unhook();
    restoreOrientation("uninstall");
    SetupCameraHook::unhook();
}

} // namespace do_a_barrel_roll::hooks
