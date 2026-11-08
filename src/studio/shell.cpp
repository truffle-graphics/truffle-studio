#include "studio/shell.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace studio {
namespace gui = truffle::gui;
namespace dock = truffle::gui::dock;

Shell::Shell(model::Document& document, gui::TextEngine& text, gui::FontId font,
             gui::TextureId atlas, SceneView* scene)
    : document_(document), ui_{context, {.engine = &text, .font = font, .atlas = atlas}},
      services_{ui_, document, [this](std::string message) { report(std::move(message)); }, scene} {
    ui_.set_error_handler([](const truffle::base::Status& status) { need(status); });
    auto root = ui_.root();
    need(context.bind_property(root.element(), gui::Property::fill, "gui.surface"));

    auto bar = root.row();
    need(context.bind_property(bar.element(), gui::Property::fill, "gui.surface"));
    switcher_bar_ = bar.row();
    auto commands = bar.row();
    undo_button = commands.button("Undo").on_click([this] { undo(); });
    redo_button = commands.button("Redo").on_click([this] { redo(); });
    undo_button.enabled(document_.can_undo());
    redo_button.enabled(document_.can_redo());

    content_ = root.column().weight(1);

    auto footer = root.row();
    status = footer.label("Ready").weight(1);
    // Restoring reparents panels, so it waits until input dispatch ends.
    reset_layout = footer.button("Reset layout").on_click([this] {
        reset_requested_ = true;
        context.request_frame();
    });
}

Shell::~Shell() { close_active(); }

void Shell::add(std::unique_ptr<Workspace> workspace) {
    Entry added;
    // The selection ring around the switcher button has no Truffle default.
    added.highlight = switcher_bar_.row().padding(2);
    const std::string id(workspace->id());
    std::string title(workspace->title());
    if (!workspace->available()) { title += " (planned)"; }
    added.button = added.highlight.button(std::move(title)).on_click([this, id] { request(id); });
    if (!workspace->available()) { added.button.enabled(false); }
    added.workspace = std::move(workspace);
    entries_.push_back(std::move(added));
    const bool multiple = std::ranges::count_if(entries_, [](const Entry& entry) {
        return entry.workspace->available();
    }) > 1;
    switcher_bar_.visible(multiple);
}

Shell::Entry* Shell::entry(std::string_view id) noexcept {
    for (auto& item : entries_) {
        if (item.workspace->id() == id) { return &item; }
    }
    return nullptr;
}

const Shell::Entry* Shell::entry(std::string_view id) const noexcept {
    return const_cast<Shell*>(this)->entry(id);
}

Workspace* Shell::find(std::string_view id) const noexcept {
    const auto* item = entry(id);
    return item ? item->workspace.get() : nullptr;
}

gui::Button Shell::switcher(std::string_view id) const {
    const auto* item = entry(id);
    return item ? item->button : gui::Button{};
}

const std::string& Shell::default_layout() const {
    static const std::string none;
    const auto* item = active_ ? entry(active_->id()) : nullptr;
    return item ? item->default_layout : none;
}

void Shell::close_active() {
    if (!active_) { return; }
    auto* item = entry(active_->id());
    item->layout = dock.save();
    highlight(context, item->highlight.element(), false);
    active_->teardown();
    if (dock.alive()) {
        const auto element = dock.element();
        need(dock.shutdown());
        need(context.destroy(element));
    }
    dock = {};
    active_ = nullptr;
}

void Shell::activate(std::string_view id) {
    auto* item = entry(id);
    if (!item) { throw std::invalid_argument("unknown workspace: " + std::string(id)); }
    if (!item->workspace->available() || active_ == item->workspace.get()) { return; }
    close_active();
    dock = need(dock::DockSpace::create(context, content_.element(), {
        .text_engine = ui_.text().engine, .font = ui_.text().font, .atlas = ui_.text().atlas}));
    gui::LayoutParams params;
    params.weight = 1;
    need(context.set_layout_params(dock.element(), params));
    item->workspace->build(services_, dock);
    if (item->default_layout.empty()) { item->default_layout = dock.save(); }
    if (!item->layout.empty()) { need(dock.restore(item->layout)); }
    highlight(context, item->highlight.element(), true);
    active_ = item->workspace.get();
    report(std::string(active_->title()) + " workspace");
}

void Shell::request(std::string_view id) {
    requested_ = id;
    context.request_frame();
}

void Shell::before_frame() {
    need(context.update_styles());
    context.update_layout(context.viewport());
    if (active_) { active_->before_frame(); }
}

void Shell::update() {
    undo_button.enabled(document_.can_undo());
    redo_button.enabled(document_.can_redo());
    if (!requested_.empty()) {
        const auto id = std::move(requested_);
        requested_.clear();
        activate(id);
    }
    if (reset_requested_) {
        reset_requested_ = false;
        if (active_) {
            need(dock.restore(default_layout()));
            report("Layout reset");
        }
    }
    if (active_) { active_->update(); }
}

void Shell::undo() {
    const auto label = document_.undo_label();
    report(document_.undo() ? "Undo " + label : "Nothing to undo");
}

void Shell::redo() {
    const auto label = document_.redo_label();
    report(document_.redo() ? "Redo " + label : "Nothing to redo");
}

bool Shell::text_editing() const { return active_ && active_->text_editing(context); }

void Shell::report(std::string message) { status.text(std::move(message)); }

} // namespace studio
