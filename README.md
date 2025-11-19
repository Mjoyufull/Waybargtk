# WaybarGtk
<img width="1924" height="64" alt="image" src="https://github.com/user-attachments/assets/f3499b4d-df81-4b36-90d5-a62c09541695" />

Fork of [Waybar](https://github.com/Alexays/Waybar) with enhanced Hyprland workspace features and GTK icon support.

This fork adds several workspace-related features that make managing multiple windows and workspaces more intuitive, especially when using special workspaces in Hyprland.

## Enhanced Features

### Workspace Improvements

**Window De-duplication**: When enabled, multiple windows of the same application class show as a single icon instead of cluttering the bar. For example, five kitty terminals display as one icon.

**Workspace Grouping**: Pairs regular workspaces with their corresponding special workspaces. Instead of showing workspaces as `[1][2][3][sp1][sp2]`, they appear grouped as `[1][sp1][2][sp2][3]` when using paired mode.

**System Icon Support**: Uses actual GTK system icons from your icon theme instead of text-based icons. Icons are loaded from your system's icon theme and scale properly.

**Special Workspace Indicators**: Special workspaces can display a custom indicator and have their icons scaled independently. Useful for visually distinguishing special workspaces from regular ones.

**Workspace Number Display**: Shows superscript numbers after the last icon in each workspace, making it clear which workspace the icons belong to. Separate options for regular and special workspaces.

### Configuration

All new features are configured through the standard Waybar config file. See `.config/config-enhanced.jsonc` for a complete example configuration with all options documented.

The main configuration options for the enhanced workspaces module:

- `deduplicate-windows`: Show one icon per application class
- `workspace-grouping`: Set to "paired" to group regular and special workspaces together
- `use-system-icons`: Enable GTK system icon loading
- `icon-size`: Size in pixels for workspace icons
- `special-workspace-indicator`: Text or icon to show before special workspace icons
- `special-workspace-icon-scale`: Scale factor for special workspace icons (0.0-1.0)
- `show-workspace-number`: Display superscript workspace number after icons
- `show-special-workspace-number`: Display superscript number for special workspaces

See the example config file for detailed comments on each option.

### Styling

The example `style.css` includes a One Dark Pro color scheme and demonstrates how to style the new workspace features. Workspace number superscripts can be styled using the `.workspace-number` and `.special-workspace-number` CSS classes.

## Standard Waybar Features

This fork includes all standard Waybar features:

- Sway (Workspaces, Binding mode, Focused window name)
- River (Mapping mode, Tags, Focused window name)
- Hyprland (Window Icons, Workspaces, Focused window name)
- Niri (Workspaces, Focused window name, Language)
- DWL (Tags, Focused window name) [requires dwl ipc patch](https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/ipc)
- Tray
- Local time
- Battery
- UPower
- Power profiles daemon
- Network
- Bluetooth
- Pulseaudio
- Privacy Info
- Wireplumber
- Disk
- Memory
- Cpu load average
- Temperature
- MPD
- Custom scripts
- Custom image
- Multiple output configuration
- And many more customizations

For standard Waybar configuration options, see the [Waybar wiki](https://github.com/Alexays/Waybar/wiki).

### Installation

Waybar is available from a number of Linux distributions:

[![Packaging status](https://repology.org/badge/vertical-allrepos/waybar.svg?columns=3&header=Waybar%20Downstream%20Packaging)](https://repology.org/project/waybar/versions)

An Ubuntu PPA with more recent versions is available
[here](https://launchpad.net/~nschloe/+archive/ubuntu/waybar).


#### Building from source

```bash
$ git clone https://github.com/Mjoyufull/Waybargtk
$ cd Waybargtk
$ meson setup build
$ ninja -C build
$ ./build/waybar
# If you want to install it
$ ninja -C build install
$ waybar
```

**Dependencies**

```
gtkmm3
jsoncpp
libsigc++
fmt
wayland
chrono-date
spdlog
libgtk-3-dev [gtk-layer-shell]
gobject-introspection [gtk-layer-shell]
libgirepository1.0-dev [gtk-layer-shell]
libpulse [Pulseaudio module]
libnl [Network module]
libappindicator-gtk3 [Tray module]
libdbusmenu-gtk3 [Tray module]
libmpdclient [MPD module]
libsndio [sndio module]
libevdev [KeyboardState module]
xkbregistry
upower [UPower battery module]
```

**Build dependencies**

```
cmake
meson
scdoc
wayland-protocols
```

On Ubuntu, you can install all the relevant dependencies using this command (tested with 19.10 and 20.04):

```
sudo apt install \
  clang-tidy \
  gobject-introspection \
  libdbusmenu-gtk3-dev \
  libevdev-dev \
  libfmt-dev \
  libgirepository1.0-dev \
  libgtk-3-dev \
  libgtkmm-3.0-dev \
  libinput-dev \
  libjsoncpp-dev \
  libmpdclient-dev \
  libnl-3-dev \
  libnl-genl-3-dev \
  libpulse-dev \
  libsigc++-2.0-dev \
  libspdlog-dev \
  libwayland-dev \
  scdoc \
  upower \
  libxkbregistry-dev
```

On Arch, you can use this command:

```
pacman -S \
  gtkmm3 \
  jsoncpp \
  libsigc++ \
  fmt \
  wayland \
  chrono-date \
  spdlog \
  gtk3 \
  gobject-introspection \
  libgirepository \
  libpulse \
  libnl \
  libappindicator-gtk3 \
  libdbusmenu-gtk3 \
  libmpdclient \
  sndio \
  libevdev \
  libxkbcommon \
  upower \
  meson \
  cmake \
  scdoc \
  wayland-protocols \
  glib2-devel
```


## Repository

Source code and issue tracking: https://github.com/Mjoyufull/Waybargtk

## License

Waybar is licensed under the MIT license. See LICENSE for more information.
