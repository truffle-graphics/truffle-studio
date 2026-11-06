#include "studio/shell.hpp"

#include "truffle/gui/themes.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace studio {
namespace gui = truffle::gui;
namespace dock = truffle::gui::dock;

Shell::Shell(model::Document& document, gui::TextEngine& text, gui::FontId font,
             gui::TextureId atlas, SceneView* scene)
    : document_(document), ui_{context, text, font, atlas},
      services_{ui_, document, [this](std::string message) { report(std::move(message)); }, scene} {
    const auto root = context.root();
    need(gui::install_default_theme(context));
    need(context.set_layout(root, std::make_shared<gui::StackLayout>()));
    need(context.bind_property(root, gui::Property::fill, "gui.surface"));

    const auto bar = ui_.row(root, 6, {8, 4, 8, 4});
    need(context.bind_property(bar, gui::Property::fill, "gui.surface"));
    switcher_bar_ = ui_.row(bar, 4);
    const auto commands = ui_.row(bar, 4);
    undo_button = ui_.button(commands, "Undo", [this] { undo(); });
    redo_button = ui_.button(commands, "Redo", [this] { redo(); });
    need(undo_button.set_enabled(document_.can_undo()));
    need(redo_button.set_enabled(document_.can_redo()));

    content_ = need(context.create(root));
    need(context.set_layout(content_, std::make_shared<gui::StackLayout>()));
    gui::LayoutParams params;
    params.weight = 1;
    need(context.set_layout_params(content_, params));

    const auto footer = ui_.row(root, 8, {8, 3, 8, 3});
    status = ui_.label(footer, "Ready");
    params = {};
    params.weight = 1;
    need(context.set_layout_params(status.element(), params));
    // Restoring reparents panels, so it waits until input dispatch ends.
    reset_layout = ui_.button(footer, "Reset layout", [this] {
        reset_requested_ = true;
        context.request_frame();
    });
}

Shell::~Shell() { close_active(); }

void Shell::add(std::unique_ptr<Workspace> workspace) {
    Entry added;
    added.highlight = ui_.row(switcher_bar_, 0, {2, 2, 2, 2});
    const std::string id(workspace->id());
    std::string title(workspace->title());
    if (!workspace->available()) { title += " (planned)"; }
    added.button = ui_.button(added.highlight, std::move(title), [this, id] { request(id); });
    if (!workspace->available()) { need(added.button.set_enabled(false)); }
    added.workspace = std::move(workspace);
    entries_.push_back(std::move(added));
    const bool multiple = std::ranges::count_if(entries_, [](const Entry& entry) {
        return entry.workspace->available();
    }) > 1;
    gui::LayoutParams params;
    if (!multiple) { params.width = 0; params.height = 0; }
    need(context.set_layout_params(switcher_bar_, params));
    need(context.set_clips_children(switcher_bar_, true));
    need(context.set_interaction(switcher_bar_, {.enabled = multiple}));
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

gui::Widget Shell::switcher(std::string_view id) const {
    const auto* item = entry(id);
    return item ? item->button : gui::Widget{};
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
    ui_.highlight(item->highlight, false);
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
    dock = need(dock::DockSpace::create(context, content_, {
        .text_engine = &ui_.text, .font = ui_.font, .atlas = ui_.atlas}));
    gui::LayoutParams params;
    params.weight = 1;
    need(context.set_layout_params(dock.element(), params));
    item->workspace->build(services_, dock);
    if (item->default_layout.empty()) { item->default_layout = dock.save(); }
    if (!item->layout.empty()) { need(dock.restore(item->layout)); }
    ui_.highlight(item->highlight, true);
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
    need(undo_button.set_enabled(document_.can_undo()));
    need(redo_button.set_enabled(document_.can_redo()));
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

void Shell::report(std::string message) { need(status.set_text(std::move(message))); }

} // namespace studio
