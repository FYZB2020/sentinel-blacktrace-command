// SENTINEL v2.3 -- high-speed orbital boundary interception.
#include "render.h"
#include "tracefx.h"

namespace {
constexpr float kTau = 6.28318530718f;

const wchar_t* BoundaryStateName(BoundaryTargetState state) {
    switch (state) {
    case BoundaryTargetState::Locked:  return L"LOCK";
    case BoundaryTargetState::Purging: return L"PURGE";
    case BoundaryTargetState::Respawn: return L"REENTRY";
    default:                           return L"TRACK";
    }
}
}

void Renderer::DrawBoundary(Sim& sim) {
    const bool reduced = sim.cfg.motion == L"reduced";
    const int tick = (int)(sim.time * 14.0);
    const int cx = 596;
    const int cy = 340;

    // LEFT -- fast denial odometer and a continuously rising command wire.
    Fill(18, 52, 258, 580, RGB(1, 7, 5));
    Frame(18, 52, 258, 580, RGB(20, 72, 52));
    Fill(18, 52, 3, 580, Blend(COL_CYAN, COL_BLACK, 0.28f));
    TextTerm(30, 60, L"BOUNDARY // DENIAL COUNTER", COL_CYAN);
    TextTerm(30, 82, L"ACTIVE PERIMETER // HIGH-SPEED MODE", COL_DIM);

    wchar_t denied[48];
    swprintf_s(denied, L"D%07llu", (unsigned long long)(sim.boundary.denied % 10000000ull));
    TextDecode(sim, 32, 110, denied, COL_WHITE, 6, 2);
    TextTerm(34, 158, L"HOSTILE EVENTS // DENIED", COL_DIM);
    wchar_t purge[80];
    swprintf_s(purge, L"PURGED %05d  //  WIRE %03d", sim.boundary.purges, sim.boundary.serial % 1000);
    TextTerm(34, 178, purge, COL_AMBER);

    int hottest = 0;
    for (int i = 1; i < 14; ++i) {
        if (sim.nodes[i].risk > sim.nodes[hottest].risk) hottest = i;
    }
    const int hotRisk = (int)(sim.nodes[hottest].risk * 100.0f);
    TextTerm(34, 212, L"PRIORITY INTERCEPT // LIVE", COL_DIM);
    Text(34, 232, sim.nodes[hottest].name.c_str(), COL_TEXT, 4);
    TextTerm(34, 266, sim.nodes[hottest].ip.c_str(), COL_RED);
    wchar_t risk[72];
    swprintf_s(risk, L"RISK %03d  //  SIG %04X", hotRisk,
               sim.boundary.targets[hottest].signature & 0xFFFFu);
    TextTerm(34, 286, risk, hotRisk > 70 ? COL_RED : COL_AMBER);
    Bar(34, 306, 220, 4, sim.nodes[hottest].risk, hotRisk > 70 ? COL_RED : COL_AMBER);

    Fill(30, 326, 234, 280, RGB(0, 5, 4));
    Frame(30, 326, 234, 280, RGB(12, 48, 35));
    TextTerm(38, 332, L"INTERCEPT STREAM // RISING", COL_GREEN);
    SaveDC(memDC);
    IntersectClipRect(memDC, PX(32), PY(350), PX(262), PY(592));
    const int rows = 16;
    const int total = (int)sim.boundary.stream.size();
    const int start = (std::max)(0, total - rows);
    const int offset = reduced ? 0 : (int)fmodf(sim.boundary.scroll, 14.0f);
    for (int row = 0; row < rows; ++row) {
        const int index = start + row;
        if (index >= total) break;
        const int ly = 350 + row * 14 - offset;
        const wchar_t* line = sim.boundary.stream[(size_t)index].c_str();
        COLORREF c = (index == total - 1) ? COL_WHITE : Blend(COL_GREEN, COL_BLACK, 0.30f);
        if (wcsstr(line, L"purge") || wcsstr(line, L"cut")) c = COL_CYAN;
        TextTerm(36, ly, line, c);
    }
    RestoreDC(memDC, -1);
    TextTerm(38, 590, L"UPSTREAM 11.8 LINES/S // BUFFER 028", COL_DIM);

    // CENTER -- two orbital rails, mutable cipher core, and a real purge lifecycle.
    Fill(286, 52, 620, 580, RGB(0, 6, 6));
    Frame(286, 52, 620, 580, RGB(17, 63, 55));
    Fill(286, 52, 620, 18, RGB(2, 15, 12));
    TextTerm(296, 56, L"BOUNDARY INTERCEPT // ORBITAL FIRE", COL_CYAN);
    DrawTraceCommand(sim, 650, 52, 244, 1);
    TextTerm(296, 80, L"TRACK > LOCK > CUT > PURGE > REDEPLOY", COL_DIM);
    wchar_t orbitStat[96];
    swprintf_s(orbitStat, L"ORBIT +0.46/-0.59  //  CUT %04d", sim.boundary.purges);
    TextTerm(718, 80, orbitStat, COL_DIM);

    for (int i = 0; i < 6; ++i) {
        int gy = 128 + i * 82;
        Line(300, gy, 892, gy, RGB(5, 24, 20), 1);
    }
    for (int i = 0; i < 7; ++i) {
        int gx = 326 + i * 90;
        Line(gx, 100, gx, 588, RGB(5, 22, 19), 1);
    }

    HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
    HGDIOBJ oldPen = SelectObject(memDC, Pen(RGB(15, 63, 53), 1));
    Ellipse(memDC, PX(cx - 242), PY(cy - 204), PX(cx + 242), PY(cy + 204));
    Ellipse(memDC, PX(cx - 170), PY(cy - 136), PX(cx + 170), PY(cy + 136));
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);

    // Bright arc segments race around the static rails; labels remain upright.
    for (int ring = 0; ring < 2; ++ring) {
        const int rx = ring == 0 ? 242 : 170;
        const int ry = ring == 0 ? 204 : 136;
        const int shift = reduced ? 12 : (int)(sim.time * (ring == 0 ? 27.0 : -34.0));
        for (int j = 0; j < 96; ++j) {
            int p = (j + shift) % 96;
            if (p < 0) p += 96;
            if (p > 15 && p < 48) continue;
            float a0 = kTau * j / 96.0f;
            float a1 = kTau * (j + 1) / 96.0f;
            Line(cx + (int)(cosf(a0) * rx), cy + (int)(sinf(a0) * ry),
                 cx + (int)(cosf(a1) * rx), cy + (int)(sinf(a1) * ry),
                 ring == 0 ? RGB(24, 121, 105) : RGB(25, 91, 112), 1);
        }
    }

    // Target links are drawn first so orbiting boxes stay legible above them.
    for (int i = 0; i < 14; ++i) {
        const BoundaryTarget& target = sim.boundary.targets[i];
        if (target.state == BoundaryTargetState::Respawn) continue;
        const int rx = i % 2 == 0 ? 242 : 170;
        const int ry = i % 2 == 0 ? 204 : 136;
        const int nx = cx + (int)(cosf(target.angle) * rx);
        const int ny = cy + (int)(sinf(target.angle) * ry);
        const int ix = cx + (int)(cosf(target.angle) * 104);
        const int iy = cy + (int)(sinf(target.angle) * 84);
        const bool hot = sim.nodes[i].risk > 0.58f || target.state != BoundaryTargetState::Tracked;
        if (!hot) continue;
        COLORREF c = target.state == BoundaryTargetState::Tracked ? RGB(48, 102, 77) : COL_RED;
        Line(nx, ny, ix, iy, Blend(c, COL_BLACK, 0.38f), 1);
        PacketLine(sim, nx, ny, ix, iy,
                   TraceFX::Cycle(sim.time, 0.88f + sim.nodes[i].risk * 0.85f,
                                  TraceFX::Unit(target.signature)), c);
        Fill(ix - 2, iy - 2, 5, 5, target.state == BoundaryTargetState::Purging ? COL_WHITE : COL_CYAN);
    }

    for (int i = 0; i < 14; ++i) {
        const BoundaryTarget& target = sim.boundary.targets[i];
        const int rx = i % 2 == 0 ? 242 : 170;
        const int ry = i % 2 == 0 ? 204 : 136;
        const int nx = cx + (int)(cosf(target.angle) * rx);
        const int ny = cy + (int)(sinf(target.angle) * ry);
        const int dynamicRisk = (int)(clampf(sim.nodes[i].risk +
            sinf((float)sim.time * 4.2f + i * 0.73f) * 0.16f, 0.01f, 0.99f) * 100.0f);

        if (target.state == BoundaryTargetState::Respawn) {
            DrawTargetBrackets(sim, nx - 24, ny - 14, 48, 28, COL_DIM);
            TextC(nx, ny - 5, L"REENTRY", COL_DIM, 0);
            continue;
        }

        float shrink = 1.0f;
        if (target.state == BoundaryTargetState::Purging)
            shrink = clampf(target.stateTimer / 0.34f, 0.12f, 1.0f);
        const int bw = (int)(94 * shrink);
        const int bh = (int)(36 * shrink);
        COLORREF c = dynamicRisk > 72 ? COL_RED : (dynamicRisk > 45 ? COL_AMBER : COL_GREEN);
        if (target.state == BoundaryTargetState::Locked) c = COL_RED;
        if (target.state == BoundaryTargetState::Purging) c = COL_WHITE;
        Fill(nx - bw / 2, ny - bh / 2, bw, bh, Blend(COL_BLACK, c, 0.10f));
        Frame(nx - bw / 2, ny - bh / 2, bw, bh, Blend(COL_BLACK, c, 0.74f));

        uint32_t signature = target.signature ^ TraceFX::Hash32((uint32_t)(tick + i * 31));
        wchar_t top[64];
        wchar_t bottom[64];
        swprintf_s(top, L"N%02d//%04X", i + 1, signature & 0xFFFFu);
        swprintf_s(bottom, L"%03d %ls", dynamicRisk, BoundaryStateName(target.state));
        TextC(nx, ny - 14, top, target.state == BoundaryTargetState::Purging ? COL_WHITE : COL_TEXT, 0);
        TextC(nx, ny + 2, bottom, c, 0);
        if (target.state == BoundaryTargetState::Locked || i == hottest)
            DrawTargetBrackets(sim, nx - 52, ny - 23, 104, 48, c);
        if (target.state == BoundaryTargetState::Purging) {
            for (int k = 0; k < 8; ++k) {
                float a = k * kTau / 8.0f + target.angle;
                int length = 18 + (int)((1.0f - shrink) * 42.0f);
                Line(nx + (int)(cosf(a) * 12), ny + (int)(sinf(a) * 9),
                     nx + (int)(cosf(a) * length), ny + (int)(sinf(a) * length),
                     k % 2 ? COL_RED : COL_WHITE, 1);
            }
        }
    }

    // Mutable cipher core: only selected glyphs mutate per tick, preserving a stable structure.
    HGDIOBJ coreOldBrush = SelectObject(memDC, Br(RGB(1, 13, 11)));
    HGDIOBJ coreOldPen = SelectObject(memDC, Pen(RGB(27, 125, 100), 1));
    Ellipse(memDC, PX(cx - 94), PY(cy - 94), PX(cx + 94), PY(cy + 94));
    SelectObject(memDC, coreOldPen);
    SelectObject(memDC, coreOldBrush);
    HGDIOBJ ringBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
    HGDIOBJ ringPen = SelectObject(memDC, Pen(RGB(35, 109, 105), 1));
    Ellipse(memDC, PX(cx - 112), PY(cy - 112), PX(cx + 112), PY(cy + 112));
    SelectObject(memDC, ringPen);
    SelectObject(memDC, ringBrush);

    for (int i = 0; i < 20; ++i) {
        float a = i * kTau / 20.0f + (reduced ? 0.0f : (float)sim.time * 0.30f);
        int gx = cx + (int)(cosf(a) * 108);
        int gy = cy + (int)(sinf(a) * 108);
        uint32_t seed = (uint32_t)(i * 7919 + ((i + tick) % 4 == 0 ? tick * 97 : tick / 4));
        wchar_t glyph[2] = { TraceFX::Glyph(TraceFX::Hash32(seed)), 0 };
        TextC(gx, gy - 6, glyph, i % 5 == 0 ? COL_CYAN : COL_DIM, 0);
    }
    for (int row = 0; row < 9; ++row) {
        wchar_t line[22];
        for (int col = 0; col < 19; ++col) {
            uint32_t seed = (uint32_t)(row * 131 + col * 17 + (((row + col) % 3 == 0) ? tick * 31 : tick / 3));
            line[col] = TraceFX::Glyph(TraceFX::Hash32(seed));
        }
        line[19] = 0;
        TextC(cx, cy - 70 + row * 15, line,
              row == 4 ? Blend(COL_CYAN, COL_BLACK, 0.30f) : Blend(COL_GREEN, COL_BLACK, 0.48f), 0);
    }
    Fill(cx - 65, cy - 26, 130, 60, RGB(0, 8, 7));
    Frame(cx - 65, cy - 26, 130, 60, RGB(30, 108, 90));
    wchar_t core[48];
    swprintf_s(core, L"D%03d", (int)(sim.time * 19.0) % 1000);
    TextC(cx, cy - 19, core, COL_WHITE, 4);
    wchar_t coreSub[64];
    swprintf_s(coreSub, L"0x%04X // ACTIVE", TraceFX::Hash32((uint32_t)tick + 0x7F3Au) & 0xFFFFu);
    TextC(cx, cy + 14, coreSub, COL_CYAN, 0);
    TextC(cx, cy + 126, L"DENY // TRACE // PURGE", COL_CYAN, 1);

    wchar_t centerFoot[112];
    swprintf_s(centerFoot, L"CIPHER %04X  //  TARGET %02d  //  PURGES %05d",
               TraceFX::Hash32((uint32_t)tick * 7919u) & 0xFFFFu,
               sim.boundary.lockedTarget + 1, sim.boundary.purges);
    TextC(cx, 594, centerFoot, COL_DIM, 0);

    // RIGHT -- permanent defense matrix plus a separate purge-event wire.
    Fill(918, 52, 344, 580, RGB(1, 7, 5));
    Frame(918, 52, 344, 580, RGB(20, 72, 52));
    Fill(918, 52, 344, 18, RGB(3, 15, 9));
    TextTerm(928, 56, L"INTERCEPT CONTROL // VERIFIED", COL_GREEN);
    TextTerm(928, 82, L"DEFENSE MATRIX // SIX LIVE LANES", COL_DIM);
    for (int i = 0; i < 6; ++i) {
        const Firewall& fw = sim.firewalls[i];
        int y = 108 + i * 30;
        COLORREF c = fw.state == FWState::Offline ? COL_RED :
                     (fw.state == FWState::Rebuilding ? COL_CYAN : COL_GREEN);
        wchar_t label[72];
        swprintf_s(label, L"L%02d %-12.12ls %03d%%", i + 1, fw.name.c_str(), (int)fw.integrity);
        TextTerm(930, y, label, c);
        Bar(1110, y + 3, 132, 4, fw.integrity / 100.0f, c);
    }
    TextTerm(928, 296, L"PURGE EVENTS // INDEPENDENT WIRE", COL_CYAN);
    Fill(928, 316, 322, 288, RGB(0, 5, 4));
    Frame(928, 316, 322, 288, RGB(12, 48, 35));
    SaveDC(memDC);
    IntersectClipRect(memDC, PX(930), PY(332), PX(1248), PY(598));
    const int eventRows = 18;
    const int eventTotal = (int)sim.boundary.events.size();
    const int eventStart = (std::max)(0, eventTotal - eventRows);
    for (int row = 0; row < eventRows; ++row) {
        int index = eventStart + row;
        int y = 334 + row * 14;
        if (index < eventTotal) {
            const wchar_t* line = sim.boundary.events[(size_t)index].c_str();
            COLORREF c = wcsstr(line, L"PURGE") ? COL_RED :
                         (wcsstr(line, L"LOCK") ? COL_AMBER : COL_TEXT);
            TextTerm(934, y, line, c);
        } else {
            wchar_t standby[80];
            swprintf_s(standby, L"CH%02d  LISTENING  //  %04X", row + 1,
                       TraceFX::Hash32((uint32_t)(tick / 4 + row * 31)) & 0xFFFFu);
            TextTerm(934, y, standby, Blend(COL_DIM, COL_BLACK, 0.20f));
        }
    }
    RestoreDC(memDC, -1);

    DrawPhaseBrief(sim, 18, 636, 258, 2);
    DrawPhaseBrief(sim, 286, 636, 620, 2);
    DrawPhaseBrief(sim, 918, 636, 344, 2);
}
