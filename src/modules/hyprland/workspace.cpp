#include <json/value.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include "modules/hyprland/workspaces.hpp"
#include "util/command.hpp"
#include "util/icon_loader.hpp"

// Helper function to convert number to superscript Unicode
static std::string toSuperscript(int num) {
  static const char* superscripts[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
  if (num < 0 || num > 9) {
    // For numbers > 9, convert each digit
    std::string result;
    std::string numStr = std::to_string(num);
    for (char c : numStr) {
      int digit = c - '0';
      if (digit >= 0 && digit <= 9) {
        result += superscripts[digit];
      }
    }
    return result;
  }
  return superscripts[num];
}

// Helper function to extract special workspace number from name
// Handles names like "sp1", "special:sp2", "sp3", etc.
static int getSpecialWorkspaceNumber(const std::string& name) {
  std::string cleanName = name;
  // Remove "special:" prefix if present
  if (cleanName.starts_with("special:")) {
    cleanName = cleanName.substr(8);
  }
  
  // Try to extract number from name like "sp1", "sp2", etc.
  std::regex spRegex("sp(\\d+)");
  std::smatch match;
  if (std::regex_search(cleanName, match, spRegex) && match.size() > 1) {
    try {
      return std::stoi(match[1].str());
    } catch (...) {
      return 0;
    }
  }
  
  // Fallback: if name is just a number, use it
  try {
    return std::stoi(cleanName);
  } catch (...) {
    return 0;
  }
}

namespace waybar::modules::hyprland {

Workspace::Workspace(const Json::Value &workspace_data, Workspaces &workspace_manager,
                     const Json::Value &clients_data)
    : m_workspaceManager(workspace_manager),
      m_id(workspace_data["id"].asInt()),
      m_name(workspace_data["name"].asString()),
      m_output(workspace_data["monitor"].asString()),  // TODO:allow using monitor desc
      m_windows(workspace_data["windows"].asInt()),
      m_isActive(true),
      m_isPersistentRule(workspace_data["persistent-rule"].asBool()),
      m_isPersistentConfig(workspace_data["persistent-config"].asBool()),
      m_ipc(IPC::inst()) {
  if (m_name.starts_with("name:")) {
    m_name = m_name.substr(5);
  } else if (m_name.starts_with("special")) {
    m_name = m_id == -99 ? m_name : m_name.substr(8);
    m_isSpecial = true;
  }

  m_button.add_events(Gdk::BUTTON_PRESS_MASK);
  m_button.signal_button_press_event().connect(sigc::mem_fun(*this, &Workspace::handleClicked),
                                               false);

  m_button.set_relief(Gtk::RELIEF_NONE);
  
  // Setup content box
  if (m_workspaceManager.enableTaskbar()) {
    m_content.set_orientation(m_workspaceManager.taskbarOrientation());
    m_content.pack_start(m_labelBefore, false, false);
  } else {
    m_content.set_center_widget(m_labelBefore);
  }
  
  // Setup window icons box for GTK icon rendering
  m_windowIconsBox.set_orientation(Gtk::ORIENTATION_HORIZONTAL);
  m_windowIconsBox.set_spacing(2);
  
  m_button.add(m_content);

  initializeWindowMap(clients_data);
}

void addOrRemoveClass(const Glib::RefPtr<Gtk::StyleContext> &context, bool condition,
                      const std::string &class_name) {
  if (condition) {
    context->add_class(class_name);
  } else {
    context->remove_class(class_name);
  }
}

std::optional<WindowRepr> Workspace::closeWindow(WindowAddress const &addr) {
  auto it = std::ranges::find_if(m_windowMap,
                                 [&addr](const auto &window) { return window.address == addr; });
  // If the vector contains the address, remove it and return the window representation
  if (it != m_windowMap.end()) {
    WindowRepr windowRepr = *it;
    m_windowMap.erase(it);
    return windowRepr;
  }
  return std::nullopt;
}

bool Workspace::handleClicked(GdkEventButton *bt) const {
  if (bt->type == GDK_BUTTON_PRESS) {
    try {
      if (id() > 0) {  // normal
        if (m_workspaceManager.moveToMonitor()) {
          m_ipc.getSocket1Reply("dispatch focusworkspaceoncurrentmonitor " + std::to_string(id()));
        } else {
          m_ipc.getSocket1Reply("dispatch workspace " + std::to_string(id()));
        }
      } else if (!isSpecial()) {  // named (this includes persistent)
        if (m_workspaceManager.moveToMonitor()) {
          m_ipc.getSocket1Reply("dispatch focusworkspaceoncurrentmonitor name:" + name());
        } else {
          m_ipc.getSocket1Reply("dispatch workspace name:" + name());
        }
      } else if (id() != -99) {  // named special
        m_ipc.getSocket1Reply("dispatch togglespecialworkspace " + name());
      } else {  // special
        m_ipc.getSocket1Reply("dispatch togglespecialworkspace");
      }
      return true;
    } catch (const std::exception &e) {
      spdlog::error("Failed to dispatch workspace: {}", e.what());
    }
  }
  return false;
}

void Workspace::initializeWindowMap(const Json::Value &clients_data) {
  m_windowMap.clear();
  for (auto client : clients_data) {
    if (client["workspace"]["id"].asInt() == id()) {
      insertWindow({client});
    }
  }
}

void Workspace::setActiveWindow(WindowAddress const &addr) {
  std::optional<long> activeIdx;
  for (size_t i = 0; i < m_windowMap.size(); ++i) {
    auto &window = m_windowMap[i];
    bool isActive = (window.address == addr);
    window.setActive(isActive);
    if (isActive) {
      activeIdx = i;
    }
  }

  auto activeWindowPos = m_workspaceManager.activeWindowPosition();
  if (activeIdx.has_value() && activeWindowPos != Workspaces::ActiveWindowPosition::NONE) {
    auto window = std::move(m_windowMap[*activeIdx]);
    m_windowMap.erase(m_windowMap.begin() + *activeIdx);
    if (activeWindowPos == Workspaces::ActiveWindowPosition::FIRST) {
      m_windowMap.insert(m_windowMap.begin(), std::move(window));
    } else if (activeWindowPos == Workspaces::ActiveWindowPosition::LAST) {
      m_windowMap.emplace_back(std::move(window));
    }
  }
}

void Workspace::insertWindow(WindowCreationPayload create_window_payload) {
  if (!create_window_payload.isEmpty(m_workspaceManager)) {
    auto repr = create_window_payload.repr(m_workspaceManager);

    if (!repr.empty() || m_workspaceManager.enableTaskbar()) {
      auto addr = create_window_payload.getAddress();
      auto it = std::ranges::find_if(
          m_windowMap, [&addr](const auto &window) { return window.address == addr; });
      // If the vector contains the address, update the window representation, otherwise insert it
      if (it != m_windowMap.end()) {
        *it = repr;
      } else {
        m_windowMap.emplace_back(repr);
      }
    }
  }
};

bool Workspace::onWindowOpened(WindowCreationPayload const &create_window_payload) {
  if (create_window_payload.getWorkspaceName() == name()) {
    insertWindow(create_window_payload);
    return true;
  }
  return false;
}

std::string &Workspace::selectIcon(std::map<std::string, std::string> &icons_map) {
  spdlog::trace("Selecting icon for workspace {}", name());
  if (isUrgent()) {
    auto urgentIconIt = icons_map.find("urgent");
    if (urgentIconIt != icons_map.end()) {
      return urgentIconIt->second;
    }
  }

  if (isActive()) {
    auto activeIconIt = icons_map.find("active");
    if (activeIconIt != icons_map.end()) {
      return activeIconIt->second;
    }
  }

  if (isSpecial()) {
    auto specialIconIt = icons_map.find("special");
    if (specialIconIt != icons_map.end()) {
      return specialIconIt->second;
    }
  }

  auto namedIconIt = icons_map.find(name());
  if (namedIconIt != icons_map.end()) {
    return namedIconIt->second;
  }

  if (isVisible()) {
    auto visibleIconIt = icons_map.find("visible");
    if (visibleIconIt != icons_map.end()) {
      return visibleIconIt->second;
    }
  }

  if (isEmpty()) {
    auto emptyIconIt = icons_map.find("empty");
    if (emptyIconIt != icons_map.end()) {
      return emptyIconIt->second;
    }
  }

  if (isPersistent()) {
    auto persistentIconIt = icons_map.find("persistent");
    if (persistentIconIt != icons_map.end()) {
      return persistentIconIt->second;
    }
  }

  auto defaultIconIt = icons_map.find("default");
  if (defaultIconIt != icons_map.end()) {
    return defaultIconIt->second;
  }

  return m_name;
}

void Workspace::update(const std::string &workspace_icon) {
  if (this->m_workspaceManager.persistentOnly() && !this->isPersistent()) {
    m_button.hide();
    return;
  }
  // clang-format off
  if (this->m_workspaceManager.activeOnly() && \
     !this->isActive() && \
     !this->isPersistent() && \
     !this->isVisible() && \
     !this->isSpecial()) {
    // clang-format on
    // if activeOnly is true, hide if not active, persistent, visible or special
    m_button.hide();
    return;
  }
  if (this->m_workspaceManager.specialVisibleOnly() && this->isSpecial() && !this->isVisible()) {
    m_button.hide();
    return;
  }
  m_button.show();

  auto styleContext = m_button.get_style_context();
  addOrRemoveClass(styleContext, isActive(), "active");
  addOrRemoveClass(styleContext, isSpecial(), "special");
  addOrRemoveClass(styleContext, isEmpty(), "empty");
  addOrRemoveClass(styleContext, isPersistent(), "persistent");
  addOrRemoveClass(styleContext, isUrgent(), "urgent");
  addOrRemoveClass(styleContext, isVisible(), "visible");
  addOrRemoveClass(styleContext, m_workspaceManager.getBarOutput() == output(), "hosting-monitor");

  // Clear the content box - make a copy of children first to avoid iterator invalidation
  auto children = m_content.get_children();
  for (auto child : children) {
    if (child) {
      m_content.remove(*child);
    }
  }

  // No longer using text-based {windows} format - all icons are GTK widgets now
  std::string windows = "";

  auto formatBefore = m_workspaceManager.formatBefore();
  
  // Use GTK widgets for icon rendering - NO nerd fonts, only system icons
  if (m_workspaceManager.enableTaskbar()) {
    // Taskbar mode - use existing taskbar rendering
    m_labelBefore.set_markup(fmt::format(fmt::runtime(formatBefore), fmt::arg("id", id()),
                                         fmt::arg("name", name()), fmt::arg("icon", workspace_icon),
                                         fmt::arg("windows", "")));
    m_content.pack_start(m_labelBefore, false, false);
    updateTaskbar(workspace_icon);
  } else {
    // Regular mode - use GTK widget rendering with system icons only
    // Create combined workspace display if we have a paired special workspace
    if (!isSpecial() && m_pairedSpecialWorkspace) {
      try {
        // Combined display: [1 icons 󰍠 special_icons]
        auto combined_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 4);
        
        Workspace* pairedSpecial = m_pairedSpecialWorkspace;
        // Check both window map AND actual window count from Hyprland to ensure we show icons
        // even when window map might not be fully updated
        bool hasSpecialWindows = pairedSpecial && (pairedSpecial->m_windows > 0 || !pairedSpecial->m_windowMap.empty());
        
        // Regular workspace number/name - always show workspace label for clickability
        // Create a fresh label (don't reuse m_labelBefore to avoid parent issues)
        auto workspace_label = Gtk::make_managed<Gtk::Label>();
        workspace_label->set_markup(fmt::format(fmt::runtime(formatBefore), fmt::arg("id", id()),
                                               fmt::arg("name", name()), fmt::arg("icon", workspace_icon),
                                               fmt::arg("windows", "")));
        workspace_label->get_style_context()->add_class("workspace-label");
        combined_box->pack_start(*workspace_label, false, false);
        
        // Regular workspace icons (create fresh widgets)
        auto regular_icons = createWindowIconWidgets();
        bool hasRegularWindows = !regular_icons.empty();
        if (hasRegularWindows) {
          for (auto icon : regular_icons) {
            if (icon && !icon->get_parent()) {
              combined_box->pack_start(*icon, false, false, 2);
            }
          }
          // Add superscript workspace number after regular workspace icons if enabled
          if (m_workspaceManager.showWorkspaceNumber() && id() > 0) {
            auto number_label = Gtk::make_managed<Gtk::Label>();
            number_label->set_markup("<span size='small' rise='5000'>" + toSuperscript(id()) + "</span>");
            number_label->get_style_context()->add_class("workspace-number");
            combined_box->pack_start(*number_label, false, false, 2);
          }
        }
        
        // Special workspace part - ALWAYS show if paired special has windows
        // This ensures special workspace content is visible even when regular workspace is empty
        if (hasSpecialWindows) {
          // Create clickable event box for special workspace
          auto special_event_box = Gtk::make_managed<Gtk::EventBox>();
          auto special_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 2);
          
          // Add indicator
          auto indicator = Gtk::make_managed<Gtk::Label>();
          indicator->set_markup(m_workspaceManager.specialWorkspaceIndicator());
          special_box->pack_start(*indicator, false, false);
          
          // Add special workspace icons (create fresh widgets, force smaller size)
          auto special_icons = pairedSpecial->createWindowIconWidgets(true);
          for (auto icon : special_icons) {
            if (icon && !icon->get_parent()) {
              special_box->pack_start(*icon, false, false, 2);
            }
          }
          
          // Add superscript workspace number after special workspace icons if enabled
          if (m_workspaceManager.showSpecialWorkspaceNumber() && pairedSpecial->id() != -99) {
            int special_id = getSpecialWorkspaceNumber(pairedSpecial->name());
            if (special_id > 0) {
              auto special_number_label = Gtk::make_managed<Gtk::Label>();
              special_number_label->set_markup("<span size='small' rise='5000'>" + toSuperscript(special_id) + "</span>");
              special_number_label->get_style_context()->add_class("special-workspace-number");
              special_box->pack_start(*special_number_label, false, false, 2);
            }
          }
          
          special_event_box->add(*special_box);
          special_event_box->add_events(Gdk::BUTTON_PRESS_MASK);
          // Click handler: go to the workspace the special is named after, then toggle it
          special_event_box->signal_button_press_event().connect(
              [this](GdkEventButton* bt) -> bool {
                return this->handleSpecialWorkspaceClick(bt);
              }, false);
          
          special_box->get_style_context()->add_class("special-workspace-section");
          combined_box->pack_start(*special_event_box, false, false);
        }
        
        // If regular workspace is empty and we have workspace number enabled, show it at the end
        if (!hasRegularWindows && m_workspaceManager.showWorkspaceNumber() && id() > 0) {
          auto number_label = Gtk::make_managed<Gtk::Label>();
          number_label->set_markup("<span size='small' rise='5000'>" + toSuperscript(id()) + "</span>");
          number_label->get_style_context()->add_class("workspace-number");
          combined_box->pack_start(*number_label, false, false, 2);
        }
        
        m_content.pack_start(*combined_box, false, false);
      } catch (const std::exception& e) {
        spdlog::warn("Error updating combined workspace {}: {}", id(), e.what());
      }
    } else if (!isSpecial()) {
      try {
        // Regular workspace without special pair - create fresh labels (don't reuse m_labelBefore)
        auto workspace_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 2);
        
        auto workspace_label = Gtk::make_managed<Gtk::Label>();
        workspace_label->set_markup(fmt::format(fmt::runtime(formatBefore), fmt::arg("id", id()),
                                             fmt::arg("name", name()), fmt::arg("icon", workspace_icon),
                                             fmt::arg("windows", "")));
        workspace_label->get_style_context()->add_class("workspace-label");
        workspace_box->pack_start(*workspace_label, false, false);
        
        // Add window icons (create fresh widgets)
        auto icons = createWindowIconWidgets();
        for (auto icon : icons) {
          if (icon && !icon->get_parent()) {
            workspace_box->pack_start(*icon, false, false, 2);
          }
        }
        
        // Add superscript workspace number after icons if enabled
        if (m_workspaceManager.showWorkspaceNumber() && id() > 0) {
          auto number_label = Gtk::make_managed<Gtk::Label>();
          number_label->set_markup("<span size='small' rise='5000'>" + toSuperscript(id()) + "</span>");
          number_label->get_style_context()->add_class("workspace-number");
          workspace_box->pack_start(*number_label, false, false, 2);
        }
        
        m_content.pack_start(*workspace_box, false, false);
      } catch (const std::exception& e) {
        spdlog::warn("Error updating workspace {}: {}", id(), e.what());
      }
    } else if (isSpecial()) {
      // Standalone special workspace (no paired regular workspace exists)
      // This happens when sp7 exists but workspace 7 has no windows/doesn't exist
      try {
        auto workspace_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 2);
        
        // Add indicator
        auto indicator = Gtk::make_managed<Gtk::Label>();
        indicator->set_markup(m_workspaceManager.specialWorkspaceIndicator());
        workspace_box->pack_start(*indicator, false, false);
        
        // Add special workspace icons
        auto icons = createWindowIconWidgets(true);
        for (auto icon : icons) {
          if (icon && !icon->get_parent()) {
            workspace_box->pack_start(*icon, false, false, 2);
          }
        }
        
        // Add superscript workspace number if enabled
        if (m_workspaceManager.showSpecialWorkspaceNumber() && id() != -99) {
          int special_id = getSpecialWorkspaceNumber(name());
          if (special_id > 0) {
            auto number_label = Gtk::make_managed<Gtk::Label>();
            number_label->set_markup("<span size='small' rise='5000'>" + toSuperscript(special_id) + "</span>");
            number_label->get_style_context()->add_class("special-workspace-number");
            workspace_box->pack_start(*number_label, false, false, 2);
          }
        }
        
        workspace_box->get_style_context()->add_class("special-workspace-section");
        m_content.pack_start(*workspace_box, false, false);
      } catch (const std::exception& e) {
        spdlog::warn("Error updating special workspace {}: {}", id(), e.what());
      }
    }
    // Note: Paired special workspaces are rendered as part of their regular workspace, not here
  }
  
  m_content.show_all();
}

bool Workspace::isEmpty() const {
  auto ignore_list = m_workspaceManager.getIgnoredWindows();
  if (ignore_list.empty()) {
    return m_windows == 0;
  }
  // If there are windows but they are all ignored, consider the workspace empty
  return std::all_of(
      m_windowMap.begin(), m_windowMap.end(),
      [this, &ignore_list](const auto &window_repr) { return shouldSkipWindow(window_repr); });
}

void Workspace::updateTaskbar(const std::string &workspace_icon) {
  // Make a copy of children list to avoid iterator invalidation
  auto children = m_content.get_children();
  for (auto child : children) {
    if (child && child != &m_labelBefore) {
      m_content.remove(*child);
    }
  }

  bool isFirst = true;
  // De-duplication: Track seen window classes
  std::unordered_set<std::string> seenClasses;

  auto processWindow = [&](const WindowRepr &window_repr) {
    if (shouldSkipWindow(window_repr)) {
      return;  // skip
    }

    // Skip if de-duplication is enabled and we've seen this class
    if (m_workspaceManager.deduplicateWindows()) {
      if (seenClasses.contains(window_repr.window_class)) {
        return;
      }
      seenClasses.insert(window_repr.window_class);
    }

    if (isFirst) {
      isFirst = false;
    } else if (m_workspaceManager.getWindowSeparator() != "") {
      auto windowSeparator = Gtk::make_managed<Gtk::Label>(m_workspaceManager.getWindowSeparator());
      m_content.pack_start(*windowSeparator, false, false);
      windowSeparator->show();
    }

    auto window_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL);
    window_box->set_tooltip_text(window_repr.window_title);
    window_box->get_style_context()->add_class("taskbar-window");
    if (window_repr.isActive) {
      window_box->get_style_context()->add_class("active");
    }
    auto event_box = Gtk::manage(new Gtk::EventBox());
    event_box->add(*window_box);
    if (m_workspaceManager.onClickWindow() != "") {
      event_box->signal_button_press_event().connect(
          sigc::bind(sigc::mem_fun(*this, &Workspace::handleClick), window_repr.address));
    }

    auto text_before = fmt::format(fmt::runtime(m_workspaceManager.taskbarFormatBefore()),
                                   fmt::arg("title", window_repr.window_title));
    if (!text_before.empty()) {
      auto window_label_before = Gtk::make_managed<Gtk::Label>(text_before);
      window_box->pack_start(*window_label_before, true, true);
    }

    if (m_workspaceManager.taskbarWithIcon()) {
      auto app_info_ = IconLoader::get_app_info_from_app_id_list(window_repr.window_class);
      int icon_size = m_workspaceManager.taskbarIconSize();
      auto window_icon = Gtk::make_managed<Gtk::Image>();
      m_workspaceManager.iconLoader().image_load_icon(*window_icon, app_info_, icon_size);
      window_box->pack_start(*window_icon, false, false);
    }

    auto text_after = fmt::format(fmt::runtime(m_workspaceManager.taskbarFormatAfter()),
                                  fmt::arg("title", window_repr.window_title));
    if (!text_after.empty()) {
      auto window_label_after = Gtk::make_managed<Gtk::Label>(text_after);
      window_box->pack_start(*window_label_after, true, true);
    }

    m_content.pack_start(*event_box, true, false);
    event_box->show_all();
  };

  if (m_workspaceManager.taskbarReverseDirection()) {
    for (auto it = m_windowMap.rbegin(); it != m_windowMap.rend(); ++it) {
      processWindow(*it);
    }
  } else {
    for (const auto &window_repr : m_windowMap) {
      processWindow(window_repr);
    }
  }
  
  // Add superscript workspace number after all windows in taskbar mode if enabled
  if (!isSpecial() && m_workspaceManager.showWorkspaceNumber() && id() > 0) {
    auto number_label = Gtk::make_managed<Gtk::Label>();
    // Use Pango markup for superscript: <span size="small" rise="5000">number</span>
    number_label->set_markup("<span size='small' rise='5000'>" + toSuperscript(id()) + "</span>");
    number_label->get_style_context()->add_class("workspace-number");
    m_content.pack_start(*number_label, false, false, 2);
    number_label->show();
  } else if (isSpecial() && m_workspaceManager.showSpecialWorkspaceNumber() && id() != -99) {
    // For special workspaces in taskbar mode - extract number from name
    int special_id = getSpecialWorkspaceNumber(name());
    if (special_id > 0) {
      auto number_label = Gtk::make_managed<Gtk::Label>();
      // Use Pango markup for superscript: <span size="small" rise="5000">number</span>
      number_label->set_markup("<span size='small' rise='5000'>" + toSuperscript(special_id) + "</span>");
      number_label->get_style_context()->add_class("special-workspace-number");
      m_content.pack_start(*number_label, false, false, 2);
      number_label->show();
    }
  }

  auto formatAfter = m_workspaceManager.formatAfter();
  if (!formatAfter.empty()) {
    m_labelAfter.set_markup(fmt::format(fmt::runtime(formatAfter), fmt::arg("id", id()),
                                        fmt::arg("name", name()),
                                        fmt::arg("icon", workspace_icon)));
    m_content.pack_end(m_labelAfter, false, false);
    m_labelAfter.show();
  }
}

bool Workspace::handleClick(const GdkEventButton *event_button, WindowAddress const &addr) const {
  if (event_button->type == GDK_BUTTON_PRESS) {
    std::string command = std::regex_replace(m_workspaceManager.onClickWindow(),
                                             std::regex("\\{address\\}"), "0x" + addr);
    command = std::regex_replace(command, std::regex("\\{button\\}"),
                                 std::to_string(event_button->button));
    auto res = util::command::execNoRead(command);
    if (res.exit_code != 0) {
      spdlog::error("Failed to execute {}: {}", command, res.out);
    }
  }
  return true;
}

bool Workspace::shouldSkipWindow(const WindowRepr &window_repr) const {
  auto ignore_list = m_workspaceManager.getIgnoredWindows();
  auto it = std::ranges::find_if(ignore_list, [&window_repr](const auto &ignoreItem) {
    return std::regex_match(window_repr.window_class, ignoreItem) ||
           std::regex_match(window_repr.window_title, ignoreItem);
  });
  return it != ignore_list.end();
}

std::vector<Gtk::Widget*> Workspace::createWindowIconWidgets(bool forceSmaller) {
  // Create a fresh set of icon widgets (not attached to any parent)
  std::vector<Gtk::Widget*> icons;
  
  // De-duplication: Track seen window classes ONLY within this workspace
  // This allows same app to appear in both regular and special workspace sections
  std::unordered_set<std::string> seenClasses;

  for (const auto &window_repr : m_windowMap) {
    if (shouldSkipWindow(window_repr)) {
      continue;
    }

    // Skip if de-duplication is enabled and we've seen this class in THIS workspace
    if (m_workspaceManager.deduplicateWindows()) {
      if (seenClasses.contains(window_repr.window_class)) {
        continue;
      }
      seenClasses.insert(window_repr.window_class);
    }

    // Only use GTK system icons - no nerd font fallbacks
    auto app_info = IconLoader::get_app_info_from_app_id_list(window_repr.window_class);
    if (app_info) {
      auto icon_widget = Gtk::make_managed<Gtk::Image>();
      int icon_size = m_workspaceManager.iconSize();
      
      // Calculate the actual size to load the icon at
      // If this is for special workspace or forceSmaller is true, scale it down
      int load_size = icon_size;
      if (forceSmaller || isSpecial()) {
        // Calculate scaled size before loading - this ensures the icon is loaded at the correct size
        load_size = static_cast<int>(icon_size * m_workspaceManager.specialWorkspaceIconScale());
      }
      
      bool loaded = m_workspaceManager.iconLoader().image_load_icon(*icon_widget, app_info, load_size);
      
      if (loaded) {
        // Also set pixel size explicitly to ensure it's displayed at the correct size
        if (forceSmaller || isSpecial()) {
          icon_widget->set_pixel_size(load_size);
        } else {
          icon_widget->set_pixel_size(icon_size);
        }
        icons.push_back(icon_widget);
      }
    }
  }

  return icons;
}

bool Workspace::handleSpecialClick(GdkEventButton *bt) {
  if (bt->type == GDK_BUTTON_PRESS && m_pairedSpecialWorkspace) {
    return m_pairedSpecialWorkspace->handleClicked(bt);
  }
  return false;
}

bool Workspace::handleSpecialWorkspaceClick(GdkEventButton *bt) {
  // Handle click on special workspace section in paired display
  // Go to the workspace that the special workspace is NAMED after (e.g., sp1 -> workspace 1)
  // Then toggle that special workspace
  // Return true to stop event propagation so button click doesn't also fire
  if (bt->type == GDK_BUTTON_PRESS && bt->button == 1) {  // Left click only
    if (m_pairedSpecialWorkspace) {
      try {
        // Extract number from special workspace name (e.g., "sp1" -> 1)
        // This determines which workspace we should go to
        int special_num = getSpecialWorkspaceNumber(m_pairedSpecialWorkspace->name());
        
        // First, go to the workspace that the special workspace is named after
        // For example: sp1 -> go to workspace 1, sp2 -> go to workspace 2
        if (special_num > 0) {
          if (m_workspaceManager.moveToMonitor()) {
            m_ipc.getSocket1Reply("dispatch focusworkspaceoncurrentmonitor " + std::to_string(special_num));
          } else {
            m_ipc.getSocket1Reply("dispatch workspace " + std::to_string(special_num));
          }
          // Then toggle the special workspace
          m_ipc.getSocket1Reply("dispatch togglespecialworkspace sp" + std::to_string(special_num));
        } else {
          // Fallback: if we can't extract a number, use the current regular workspace
          if (id() > 0) {
            if (m_workspaceManager.moveToMonitor()) {
              m_ipc.getSocket1Reply("dispatch focusworkspaceoncurrentmonitor " + std::to_string(id()));
            } else {
              m_ipc.getSocket1Reply("dispatch workspace " + std::to_string(id()));
            }
          }
          // Toggle the special workspace using its name directly
          m_ipc.getSocket1Reply("dispatch togglespecialworkspace " + m_pairedSpecialWorkspace->name());
        }
        return true;  // Stop event propagation
      } catch (const std::exception &e) {
        spdlog::error("Failed to handle special workspace click: {}", e.what());
      }
    }
  }
  return false;  // Allow event to propagate if not handled
}

}  // namespace waybar::modules::hyprland
