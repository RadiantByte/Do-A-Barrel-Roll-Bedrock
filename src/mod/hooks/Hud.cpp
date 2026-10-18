// HUD: artificial horizon + momentum crosshair, drawn after the UI as 1-GUI-pixel filled rectangles (there is no
// line primitive).
//
// MOMENTUM CROSSHAIR (like Java, the REAL vanilla crosshair is moved): the crosshair is the "cursor_renderer" custom
// renderer of the HUD ("hud_cursor" control), class HudCursorRenderer (vftable RVA 0xea3eee0, constructed by the
// factory at RVA 0x45a2870 from the "cursor_renderer" name compare in the custom-renderer registration function
// at RVA 0x4587a80). Its render(MinecraftUIRenderContext&, IClientInstance&, UIControl& owner, int) is RVA
// 0x8db9530 (vtable slot 16, reached through the UIRenderContext thunk in slot 5). It places the 16x16 cross_hair
// texture at x = int((guiW - owner.size.x) * 0.5), y = int((guiH - owner.size.y) * 0.5) (movss [owner+0x48/0x4c],
// i.e. UIControl::mSize, RVA 0x8db9bf9..0x8db9c3f) and blits it. So the hook shrinks owner.mSize by 2*offset around
// the original call (x' = x + offset) and restores it afterwards. The momentum line is drawn from the screen centre
// to that offset in AfterUIRenderEvent, only if the crosshair renderer ran in the last frames.
// ASSUMPTIONS: render-context coordinates are GUI-scaled units and GuiData
// ScreenSizeData::clientUIScreenSize is the size in those units; the HUD ScreenView is named "hud_screen".

#include "mod/hooks/State.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/HudCursorRenderer.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mod/core/Hud.h"

#include <vector>

namespace do_a_barrel_roll::hooks {

namespace {

ll::event::ListenerPtr listener;

// Time of the last HudCursorRenderer::render call that applied the momentum offset (steady ms), and the offset.
std::int64_t lastCursorMs = -1000000;
int          cursorOffX = 0, cursorOffY = 0;

bool momentumActive() {
    auto& cfg = state::config;
    return state::hudVisible && cfg.showMomentumWidget && cfg.momentumBasedMouse;
}

void rect(MinecraftUIRenderContext& ctx, float x0, float y0, float x1, float y1, mce::Color const& color, float alpha) {
    RectangleArea r{};
    r._x0 = x0;
    r._x1 = x1;
    r._y0 = y0;
    r._y1 = y1;
    ctx.fillRectangle(r, color, alpha);
}

// Java draws each pixel with an inverting blend (visible on any background). There is no such blend here, so
// every dot gets a dark 1-unit halo (drawn in a first pass) under the white dot.
void line(MinecraftUIRenderContext& ctx, core::IntLine const& l, bool halo) {
    static mce::Color white{1.0f, 1.0f, 1.0f, 1.0f};
    static mce::Color black{0.0f, 0.0f, 0.0f, 1.0f};
    core::forBresenhamLine(l.x0, l.y0, l.x1, l.y1, [&](int x, int y) {
        auto fx = static_cast<float>(x);
        auto fy = static_cast<float>(y);
        if (halo) rect(ctx, fx - 1, fy - 1, fx + 2, fy + 2, black, 0.5f);
        else rect(ctx, fx, fy, fx + 1, fy + 1, white, 1.0f);
    });
}

void onRender(ll::event::AfterUIRenderEvent& e) {
    static bool loggedFirstEvent = false;
    static bool loggedFirstDraw  = false;
    if (!loggedFirstEvent) {
        loggedFirstEvent = true;
        state::debugLog("hud: AfterUIRenderEvent fired, first screen='{}'", e.screenView().getScreenName());
    }
    if (!state::hudVisible) return;
    auto& cfg = state::config;
    if (!cfg.showHorizon && !(cfg.showMomentumWidget && cfg.momentumBasedMouse)) return;
    if (e.screenView().getScreenName() != "hud_screen") return;
    auto client = ll::service::getClientInstance();
    if (!client) return;
    auto* player = client->getLocalPlayer();
    if (!player) return;

    auto& ctx = e.uiRenderContext();

    // Screen size in GUI-scaled units: prefer the render context's own full clip rectangle (exactly the space
    // fillRectangle draws in), fall back to GuiData's UI screen size.
    auto  clip = ctx.getFullClippingRectangle();
    float cw = clip._x1 - clip._x0, ch = clip._y1 - clip._y0;
    Vec2  guiSize{0, 0};
    guiSize = client->getGuiData()->mScreenSizeData->clientUIScreenSize;
    int w, h;
    if (cw >= 16.0f && ch >= 16.0f) {
        w = static_cast<int>(cw);
        h = static_cast<int>(ch);
    } else {
        w = static_cast<int>(guiSize.x);
        h = static_cast<int>(guiSize.y);
    }
    if (w <= 0 || h <= 0) return;

    std::vector<core::IntLine> lines;

    if (cfg.showHorizon) {
        auto ps = state::snapshot(*player);
        for (auto const& l : core::horizonLines(w, h, state::controller.getRoll(), ps.pitch)) lines.push_back(l);
    }
    if (cfg.showMomentumWidget && cfg.momentumBasedMouse) {
        // The real crosshair (moved by HudCursorRenderHook) is centred at (w/2, h/2); Java's line starts at h/2 - 1
        // because its crosshair is 15px, so shift by one to start at the middle of the 16px Bedrock sprite.
        // Only draw while the crosshair renderer is actually running (it is hidden e.g. in third person).
        auto m = core::momentumWidget(w, h, state::controller.mouseTurnVec());
        if (m.hasLine && state::nowMs() - lastCursorMs < 250) {
            m.line.y0 += 1;
            m.line.y1 += 1;
            lines.push_back(m.line);
        }
        static std::int64_t lastLineLog = -1000000;
        if (state::nowMs() - lastLineLog > 2000) {
            lastLineLog = state::nowMs();
            state::debugLog(
                "hud: momentum vec=({:.3f},{:.3f}) line={} cursorHookAgeMs={} offset=({},{})",
                state::controller.mouseTurnVec().x, state::controller.mouseTurnVec().y, m.hasLine,
                state::nowMs() - lastCursorMs, cursorOffX, cursorOffY
            );
        }
    }

    if (!loggedFirstDraw) {
        loggedFirstDraw = true;
        state::debugLog(
            "hud: first draw. clip=({:.1f},{:.1f})-({:.1f},{:.1f}) guiUISize=({:.1f},{:.1f}) used={}x{} lines={} "
            "horizon={} momentum={}",
            clip._x0, clip._y0, clip._x1, clip._y1, guiSize.x, guiSize.y, w, h, lines.size(), cfg.showHorizon,
            cfg.showMomentumWidget && cfg.momentumBasedMouse
        );
    }

    for (auto const& l : lines) line(ctx, l, true);
    for (auto const& l : lines) line(ctx, l, false);
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    HudCursorRenderHook,
    ll::memory::HookPriority::Normal,
    HudCursorRenderer,
    &HudCursorRenderer::$render,
    void,
    ::MinecraftUIRenderContext& renderContext,
    ::IClientInstance&          client,
    ::UIControl&                owner,
    int                         pass
) {
    static int logCount = 0;
    static std::int64_t lastLogMs = -1000000;
    if (!momentumActive()) return origin(renderContext, client, owner, pass);

    auto m  = core::momentumWidget(0, 0, state::controller.mouseTurnVec());
    int  ox = m.crosshairOffsetX, oy = m.crosshairOffsetY;
    auto& size = owner.mSize.get();
    float sx = size.x, sy = size.y;
    // x = (guiW - size.x) / 2 inside the original, so size -= 2 * offset moves the crosshair by +offset.
    size.x = sx - 2.0f * static_cast<float>(ox);
    size.y = sy - 2.0f * static_cast<float>(oy);
    origin(renderContext, client, owner, pass);
    size.x = sx;
    size.y = sy;

    lastCursorMs = state::nowMs();
    cursorOffX   = ox;
    cursorOffY   = oy;
    if (logCount < 3 || lastCursorMs - lastLogMs > 2000) {
        lastLogMs = lastCursorMs;
        logCount++;
        state::debugLog(
            "hud: HudCursorRenderer::render hooked, owner.size=({:.1f},{:.1f}) offset=({},{}) mSize@+{:#x}", sx, sy,
            ox, oy, reinterpret_cast<std::uintptr_t>(&owner.mSize.get()) - reinterpret_cast<std::uintptr_t>(&owner)
        );
    }
}

void installHud() {
    HudCursorRenderHook::hook();
    listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>(onRender);
}

void uninstallHud() {
    HudCursorRenderHook::unhook();
    if (listener) ll::event::EventBus::getInstance().removeListener(listener);
    listener.reset();
}

} // namespace do_a_barrel_roll::hooks
