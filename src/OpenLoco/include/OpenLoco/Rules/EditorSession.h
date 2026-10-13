#pragma once
#include "Editor.h"
#include "Ui/Window.h"
#include "Ui/Widgets/ButtonWidget.h"
#include "Ui/Widgets/ScrollViewWidget.h"
#include <memory>

namespace OpenLoco::Rules
{
    namespace EditorIds
    {
        constexpr Ui::WidgetId palette{ "rules_palette" }, check{ "rules_check" }, compare{ "rules_compare" }, stop{ "rules_stop" }, cargo{ "rules_cargo" }, number{ "rules_number" }, join{ "rules_join" }, extra{ "rules_extra" }, target{ "rules_target" }, name{ "rules_name" }, limit{ "rules_limit" }, first{ "rules_first" }, last{ "rules_last" }, bin{ "rules_bin" }, details{ "rules_details" }, actions{ "rules_actions" };
    }
    // Appended after the host's existing widgets; indices stay below the native 64-bit mask limit.
    constexpr auto inlineEditorWidgets()
    {
        using namespace Ui;
        return makeWidgets(
            Widgets::ScrollView(EditorIds::palette, { 0, 0 }, { 88, 100 }, WindowColour::secondary, Scrollbars::vertical),
            Widgets::Button(EditorIds::check, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_check),
            Widgets::Button(EditorIds::compare, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_compare),
            Widgets::Button(EditorIds::stop, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_stop),
            Widgets::Button(EditorIds::cargo, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_cargo),
            Widgets::Button(EditorIds::number, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_number),
            Widgets::Button(EditorIds::join, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_join),
            Widgets::Button(EditorIds::extra, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_extra),
            Widgets::Button(EditorIds::target, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_target),
            Widgets::Button(EditorIds::name, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_name),
            Widgets::Button(EditorIds::limit, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_minutes),
            Widgets::Button(EditorIds::first, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_first_row),
            Widgets::Button(EditorIds::last, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_last_row),
            Widgets::Button(EditorIds::bin, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_bin),
            Widgets::Button(EditorIds::details, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_details),
            Widgets::Button(EditorIds::actions, { 0, 0 }, { 20, 16 }, WindowColour::secondary, StringIds::rules_actions));
    }
    enum class EditorDrag : uint8_t
    {
        none,
        palette,
        section,
        resize,
        row,
        selection
    };
    struct InlineLayout
    {
        EditorRect palette, canvas, controls, toolbar;
    };
    InlineLayout inlineLayout(int16_t left, int16_t top, int16_t right, int16_t bottom, bool expanded, bool details);
    Ui::Size expandedEditorSize(Ui::Size previous, Ui::Size screen, bool line);

    class EditorSession
    {
        struct State;
        std::unique_ptr<State> state;

    public:
        EditorSession();
        ~EditorSession();
        EditorSession(const EditorSession&) = delete;
        EditorSession& operator=(const EditorSession&) = delete;
        void bind(Owner owner, const Program& program, uint16_t vehicle = UINT16_MAX, bool writable = true);
        void configure(Ui::Window& window, Owner owner, uint16_t vehicle, Ui::WidgetIndex_t canvas, uint8_t canvasScroll, Ui::WidgetIndex_t firstWidget, uint8_t paletteScroll, bool writable);
        void prepare(Ui::Window& window, int16_t left, int16_t top, int16_t right, int16_t bottom);
        void toggle(Ui::Window& window, Ui::Size screen = {});
        void suspend(Ui::Window& window, bool restore = true);
        bool expanded() const;
        bool showingProgram() const;
        const Program& program() const;
        const ProgramLayout& layout() const;
        Key selectedBlock() const;
        std::optional<std::pair<size_t, size_t>> selection() const;
        void selectRows(size_t first, size_t last);
        void selectBlock(Key block);
        void startDrag(EditorDrag kind, size_t first, size_t last, Key block = 0, Kind paletteKind = Kind::group);
        std::optional<Edit> dropProposal(int16_t y) const;
        void cancel();
        bool dragging() const;
        bool drawScroll(Ui::Window& window, Gfx::DrawingContext& context, uint32_t scroll);
        void draw(Ui::Window& window, Gfx::DrawingContext& context);
        bool scrollSize(uint32_t scroll, int32_t& width, int32_t& height) const;
        bool mouseDown(Ui::Window& window, int16_t x, int16_t y, uint8_t scroll);
        bool mouseDrag(Ui::Window& window, int16_t x, int16_t y, uint8_t scroll);
        bool mouseRelease(Ui::Window& window, int16_t x, int16_t y, uint8_t scroll);
        bool buttonDown(Ui::Window& window, Ui::WidgetIndex_t widget, Ui::WidgetId id);
        bool buttonUp(Ui::Window& window, Ui::WidgetIndex_t widget, Ui::WidgetId id);
        bool dropdown(Ui::Window& window, Ui::WidgetIndex_t widget, int16_t item);
        bool textInput(Ui::Window& window, Ui::WidgetIndex_t widget, const char* text);
        bool escape(Ui::Window& window);
        std::optional<FormatArguments> tooltip(Ui::Window& window);
    };
    EditorSession& editorSession(Ui::Window& window);
    void closeEditorSession(Ui::Window& window);
}
