#pragma once
#include "imgui.h"
#include <vector>
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
//  GradientPoint
// ---------------------------------------------------------------------------
struct GradientPoint
{
    float  position; // [0, 1]
    ImVec4 color;    // RGBA
};

inline bool IsMean(const ImVec4 prev, const ImVec4 curr, const ImVec4 next)
{
    const bool rMean = std::abs(prev.x - curr.x) < 0.01f && std::abs(curr.x - next.x) < 0.01f;
    const bool gMean = std::abs(prev.y - curr.y) < 0.01f && std::abs(curr.y - next.y) < 0.01f;
    const bool bMean = std::abs(prev.z - curr.z) < 0.01f && std::abs(curr.z - next.z) < 0.01f;
    const bool aMean = std::abs(prev.w - curr.w) < 0.01f && std::abs(curr.w - next.w) < 0.01f;
    const bool isAZero = prev.w <= 0.01f && next.w <= 0.01f && curr.w <= 0.01f;

    return (rMean && gMean && bMean && aMean) || isAZero;
}


// ---------------------------------------------------------------------------
//  GradientEditor  –  semi-retained widget
//
//  Owns the point list. Call Draw() every frame; it returns true when the
//  gradient was modified so you can bake/upload a texture.
//
//  Usage:
//      static GradientEditor grad({ {0.f,{0,0,0,1}}, {1.f,{1,1,1,1}} });
//      if (grad.Draw("My Gradient"))
//          RebakeTexture(grad.Points());
// ---------------------------------------------------------------------------
class GradientEditor
{
public:
    // ---- Construction -------------------------------------------------------
    GradientEditor() { m_points = { {0.f,{0,0,0,1}}, {1.f,{1,1,1,1}} }; }
    explicit GradientEditor(std::initializer_list<GradientPoint> pts)
        : m_points(pts) { Sort(); }
    explicit GradientEditor(std::vector<GradientPoint> pts)
        : m_points(std::move(pts)) { Sort(); }

    // The colors will be considered evenly spaced and the minimum amount of points will be saved (if a point is the mean of the points left and right of it, it will be removed).
    explicit GradientEditor(const std::vector<ImVec4>& colors)
    {
        m_points.clear();
        if (colors.empty())
        {
            m_points.push_back({ 0.f, {0,0,0,1} });
            m_points.push_back({ 1.f, {1,1,1,1} });
        }
        else
        {
            const float step = 1.f / (colors.size() - 1);
            for (size_t i = 0; i < colors.size(); ++i)
            {
                if (i >= 1 && i < colors.size() - 1 && IsMean(colors[i - 1], colors[i], colors[i + 1]))
                {
                    continue;
                }
                m_points.push_back({ i * step, colors[i] });
            }
            Sort();
        }
    }

    // ---- Accessors ----------------------------------------------------------
    const std::vector<GradientPoint>& Points() const { return m_points; }
          std::vector<GradientPoint>& Points()        { return m_points; }

    // Sample the gradient at t in [0,1]
    ImVec4 Sample(float t) const { return SamplePoints(m_points, t); }

    // ---- Draw  (call every frame) -------------------------------------------
    // Returns true if the gradient was modified this frame.
    bool Draw(const char* label, float width = 0.f)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGuiIO&          io    = ImGui::GetIO();
        ImDrawList*       dl    = ImGui::GetWindowDrawList();

        // --- Geometry --------------------------------------------------------
        const float W           = (width > 0.f) ? width : ImGui::CalcItemWidth();
        const float BAR_H       = ImGui::GetFrameHeight();
        const float HANDLE_R    = style.GrabRounding > 0.f
                                    ? std::max(style.GrabMinSize * 0.5f, 5.f)
                                    : std::max(style.GrabMinSize * 0.5f, 5.f);
        const float GAP         = style.ItemInnerSpacing.y;
        const float WIDGET_H    = BAR_H + GAP + HANDLE_R * 2.f + 2.f;
        const float CHECKER_SZ  = 6.f;

        ImGui::PushID(label);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 barMin = origin;
        const ImVec2 barMax = { barMin.x + W, barMin.y + BAR_H };
        const float  handleY = barMax.y + GAP + HANDLE_R;

        bool modified = false;

        // --- Input regions via InvisibleButton -------------------------------
        // Bar
        ImGui::SetCursorScreenPos(barMin);
        ImGui::InvisibleButton("##bar", { W, BAR_H });
        const bool barHovered    = ImGui::IsItemHovered();
        const bool barLClicked   = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        // Handle strip
        ImGui::SetCursorScreenPos({ barMin.x, barMax.y + GAP });
        ImGui::InvisibleButton("##handles", { W, HANDLE_R * 2.f + 2.f });
        const bool stripHovered  = ImGui::IsItemHovered();

        const ImVec2 mp = io.MousePos;

        // --- Helpers ---------------------------------------------------------
        auto PosToX  = [&](float p) { return barMin.x + p * W; };
        auto XToPos  = [&](float x) { return std::clamp((x - barMin.x) / W, 0.f, 1.f); };

        auto HitHandle = [&]() -> int          // returns index or -1
        {
            const float R2 = (HANDLE_R + 3.f) * (HANDLE_R + 3.f);
            for (int i = 0; i < (int)m_points.size(); ++i)
            {
                float dx = mp.x - PosToX(m_points[i].position);
                float dy = mp.y - handleY;
                if (dx * dx + dy * dy <= R2) return i;
            }
            return -1;
        };

        // --- Drag continuation -----------------------------------------------
        // m_dragIdx is set when a drag starts and cleared on mouse-up.
        // Because we own the vector, we move the *actual element* rather than
        // searching by fingerprint — no identity ambiguity.
        if (m_dragIdx >= 0)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                m_points[m_dragIdx].position = XToPos(mp.x);
                // Re-sort and keep m_dragIdx pointing at the same element.
                // We do a partial insertion-sort step instead of a full sort
                // so the dragged element stays selected even when it crosses
                // neighbours.
                ResortDragged();
                modified = true;
            }
            else
            {
                m_dragIdx = -1;
            }
        }

        // --- Bar left-click → insert -----------------------------------------
        if (barLClicked && m_dragIdx < 0)
        {
            // Only insert if we hit the bar in the colored part
            if (mp.x >= barMin.x && mp.x <= barMax.x && mp.y >= barMin.y && mp.y <= barMax.y)
            {
                float t = XToPos(mp.x);
                GradientPoint np{ t, SamplePoints(m_points, t) };
                m_points.push_back(np);
                Sort();
                // Select the newly inserted point
                m_selectedIdx = (int)(std::find_if(m_points.begin(), m_points.end(),
                    [&](const GradientPoint& p){ return p.position == t; })
                    - m_points.begin());
                modified = true;
            }
        }

        // --- Handle strip interactions ----------------------------------------
        if (stripHovered && m_dragIdx < 0)
        {
            int h = HitHandle();

            if (h >= 0)
            {
                // Left-click → select + start drag
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    m_selectedIdx = h;
                    m_dragIdx     = h;
                }

                // Right-click → delete (need > 1 point)
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)
                    && m_points.size() > 1)
                {
                    m_points.erase(m_points.begin() + h);
                    m_selectedIdx = std::clamp(m_selectedIdx,
                                            0, (int)m_points.size() - 1);
                    modified = true;
                }

                // Double-click → popup editor
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    m_selectedIdx = h;
                    m_dragIdx     = -1;
                    ImGui::OpenPopup("##gp_edit");
                }
            }
        }

        // --- Drawing ---------------------------------------------------------

        // Checkerboard
        DrawChecker(dl, barMin, barMax, CHECKER_SZ);

        // Gradient quads
        {
            const int SEGS = 128;
            for (int i = 0; i < SEGS; ++i)
            {
                float t0 = (float)i       / SEGS;
                float t1 = (float)(i + 1) / SEGS;
                float x0 = barMin.x + t0 * W;
                float x1 = barMin.x + t1 * W;
                ImU32 c0 = ToU32(SamplePoints(m_points, t0));
                ImU32 c1 = ToU32(SamplePoints(m_points, t1));
                dl->AddRectFilledMultiColor(
                    {x0, barMin.y}, {x1, barMax.y}, c0, c1, c1, c0);
            }
        }

        // Hover crosshair hint on bar
        if (barHovered && m_dragIdx < 0)
            dl->AddLine({mp.x, barMin.y}, {mp.x, barMax.y},
                        ImGui::GetColorU32(ImGuiCol_Text, 0.35f), 1.f);

        // Handles
        for (int i = 0; i < (int)m_points.size(); ++i)
        {
            const float hx   = PosToX(m_points[i].position);
            const bool  isSel = (i == m_selectedIdx);

            // Connector line from bar bottom to handle
            dl->AddLine({hx, barMax.y}, {hx, handleY - HANDLE_R},
                        ImGui::GetColorU32(ImGuiCol_Separator), 1.f);

            // Shadow/outline ring (slightly larger, dark)
            dl->AddCircleFilled({hx, handleY}, HANDLE_R + 1.5f,
                                IM_COL32(0, 0, 0, 120));

            // Color fill
            dl->AddCircleFilled({hx, handleY}, HANDLE_R,
                                ToU32(m_points[i].color));

            // Selection ring — use ImGuiCol_SliderGrabActive when selected,
            //                   ImGuiCol_FrameBgHovered otherwise
            ImU32 ringCol = isSel
                ? ImGui::GetColorU32(ImGuiCol_SliderGrabActive)
                : ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
            float ringW   = isSel ? 2.f : 1.f;
            dl->AddCircle({hx, handleY}, HANDLE_R, ringCol, 0, ringW);
        }

        // --- Popup editor ----------------------------------------------------
        if (ImGui::BeginPopup("##gp_edit"))
        {
            if (m_selectedIdx >= 0 && m_selectedIdx < (int)m_points.size())
            {
                GradientPoint& pt = m_points[m_selectedIdx];

                ImGui::TextDisabled("Stop %d", m_selectedIdx);
                ImGui::Separator();

                float col[4] = { pt.color.x, pt.color.y, pt.color.z, pt.color.w };
                if (ImGui::ColorPicker4("##gp_col", col,
                        ImGuiColorEditFlags_AlphaBar |
                        ImGuiColorEditFlags_AlphaPreview |
                        ImGuiColorEditFlags_NoSidePreview))
                {
                    pt.color = {col[0], col[1], col[2], col[3]};
                    modified = true;
                }

                ImGui::Spacing();
                if (ImGui::SliderFloat("Position##gp_pos",
                                       &pt.position, 0.f, 1.f, "%.3f"))
                {
                    Sort();
                    modified = true;
                }
            }
            ImGui::EndPopup();
        }

        // --- Label -----------------------------------------------------------
        const bool showLabel = !(label[0] == '#' && label[1] == '#');
        if (showLabel)
        {
            ImGui::SetCursorScreenPos(
                { barMax.x + style.ItemInnerSpacing.x,
                  origin.y + (WIDGET_H - ImGui::GetTextLineHeight()) * 0.5f });
            ImGui::TextUnformatted(label);
        }

        // Advance layout cursor
        ImGui::SetCursorScreenPos({ origin.x, origin.y + WIDGET_H });
        ImGui::Dummy({0.f, 0.f});

        ImGui::PopID();
        return modified;
    }

private:
    std::vector<GradientPoint> m_points;
    int m_selectedIdx = 0;
    int m_dragIdx     = -1;    // index of point currently being dragged; -1 = none

    // ---- Sorting ------------------------------------------------------------
    void Sort()
    {
        // Preserve which point is selected across the sort by tracking its
        // position before and matching after.
        float selPos = (m_selectedIdx >= 0 && m_selectedIdx < (int)m_points.size())
                       ? m_points[m_selectedIdx].position : -1.f;

        std::stable_sort(m_points.begin(), m_points.end(),
            [](const GradientPoint& a, const GradientPoint& b){
                return a.position < b.position;
            });

        if (selPos >= 0.f)
        {
            for (int i = 0; i < (int)m_points.size(); ++i)
                if (m_points[i].position == selPos) { m_selectedIdx = i; break; }
        }
    }

    // Bubble the dragged element one step left/right to maintain sort order
    // while keeping m_dragIdx pointing at it — avoids a full re-sort that
    // would lose the drag index.
    void ResortDragged()
    {
        if (m_dragIdx < 0) return;
        float pos = m_points[m_dragIdx].position;

        // Bubble right
        while (m_dragIdx + 1 < (int)m_points.size() &&
               m_points[m_dragIdx + 1].position < pos)
        {
            std::swap(m_points[m_dragIdx], m_points[m_dragIdx + 1]);
            ++m_dragIdx;
        }
        // Bubble left
        while (m_dragIdx - 1 >= 0 &&
               m_points[m_dragIdx - 1].position > pos)
        {
            std::swap(m_points[m_dragIdx], m_points[m_dragIdx - 1]);
            --m_dragIdx;
        }

        m_selectedIdx = m_dragIdx;
    }

    // ---- Static helpers -----------------------------------------------------
    static ImVec4 Lerp4(const ImVec4& a, const ImVec4& b, float t)
    {
        return { a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t,
                 a.z+(b.z-a.z)*t, a.w+(b.w-a.w)*t };
    }

    static ImVec4 SamplePoints(const std::vector<GradientPoint>& pts, float t)
    {
        if (pts.empty())              return {0,0,0,1};
        if (pts.size() == 1
            || t <= pts.front().position) return pts.front().color;
        if (t >= pts.back().position)     return pts.back().color;

        for (size_t i = 1; i < pts.size(); ++i)
        {
            if (t <= pts[i].position)
            {
                float range = pts[i].position - pts[i-1].position;
                float local = (range > 1e-6f)
                              ? (t - pts[i-1].position) / range : 0.f;
                return Lerp4(pts[i-1].color, pts[i].color, local);
            }
        }
        return pts.back().color;
    }

    static ImU32 ToU32(const ImVec4& c)
    {
        return IM_COL32(
            (int)(std::clamp(c.x,0.f,1.f)*255.f+.5f),
            (int)(std::clamp(c.y,0.f,1.f)*255.f+.5f),
            (int)(std::clamp(c.z,0.f,1.f)*255.f+.5f),
            (int)(std::clamp(c.w,0.f,1.f)*255.f+.5f));
    }

    static void DrawChecker(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float sz)
    {
        const ImU32 c0 = IM_COL32(80,  80,  80,  255);
        const ImU32 c1 = IM_COL32(160, 160, 160, 255);
        int cols = (int)((mx.x - mn.x) / sz) + 1;
        int rows = (int)((mx.y - mn.y) / sz) + 1;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                ImVec2 p0 { mn.x + c*sz, mn.y + r*sz };
                ImVec2 p1 { std::min(p0.x+sz, mx.x), std::min(p0.y+sz, mx.y) };
                dl->AddRectFilled(p0, p1, ((r+c)&1) ? c1 : c0);
            }
    }
};