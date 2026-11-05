#include "studio/shell.hpp"

#include <memory>
#include <stdexcept>

namespace studio {
namespace gui = truffle::gui;
namespace dock = truffle::gui::dock;

namespace {
constexpr gui::Color active_accent{0.24F, 0.45F, 0.85F, 1};
constexpr gui::Color no_fill{0, 0, 0, 0};

} // namespace

Shell::Shell(model::Document& document, gui::TextEngine& text, gui::FontId font,
             gui::TextureId atlas)
    : document_(document), ui_{context, text, font, atlas},
      services_{ui_, document, [this](std::string message) { report(std::move(message)); }} {
    const auto root = context.root();
    need(context.set_layout(root, std::make_shared<gui::StackLayout>()));
    need(context.set_decoration(root, {.fill = {0.07F, 0.07F, 0.08F, 1}}));

    const auto bar = ui_.row(root, 16, {10, 6, 10, 6});
    need(context.set_decoration(bar, {.fill = {0.10F, 0.10F, 0.11F, 1}}));
    static_cast<void>(ui_.label(bar, "Truffle Studio"));
    switcher_bar_ = ui_.row(bar, 4);
    const auto commands = ui_.row(bar, 8);
    undo_button = ui_.button(commands, "Undo", [this] { undo(); });
    redo_button = ui_.button(commands, "Redo", [this] { redo(); });
    // Restoring reparents panels, so it waits until input dispatch ends.
    reset_layout = ui_.button(commands, "Reset layout", [this] {
        reset_requested_ = true;
        context.request_frame();
    });

    content_ = need(context.create(root));
    need(context.set_layout(content_, std::make_shared<gui::StackLayout>()));
    gui::LayoutParams params;
    params.weight = 1;
    need(context.set_layout_params(content_, params));

    status = ui_.label(root, "Ready");
    params = {};
    params.padding = {10, 4, 10, 4};
    need(context.set_layout_params(status.element(), params));
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
    need(context.set_decoration(item->highlight, {.fill = no_fill}));
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
    need(context.set_decoration(item->highlight, {.fill = active_accent}));
    active_ = item->workspace.get();
    report(std::string(active_->title()) + " workspace");
}

void Shell::request(std::string_view id) {
    requested_ = id;
    context.request_frame();
}

void Shell::update() {
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
