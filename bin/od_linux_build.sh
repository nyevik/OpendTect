#!/bin/bash
#________________________________________________________________________
#
# Copyright:    (C) 1995-2026 dGB Beheer B.V.
# License:      https://dgbes.com/licensing
#________________________________________________________________________
#
# od_linux_build.sh
# Linux TUI/CLI helper for configuring, building and installing OpendTect
#

#fail loud and early, and treat unset variables as errors
set -euo pipefail

if (( BASH_VERSINFO[0] < 4 )); then
    echo "This script requires bash 4 or newer." >&2
    exit 1
fi

readonly OD_SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly OD_SOURCE_DIR="$(cd -- "${OD_SCRIPT_DIR}/.." && pwd)"
readonly OD_BUILD_ROOT="${OD_SOURCE_DIR}/.odbuild/linux"
readonly OD_BACKTITLE="OpendTect Linux Build Assistant"

declare -a DEP_KEYS=(qt osg hdf5 proj sqlite)
declare -A DEP_LABELS=(
    [qt]="Qt"
    [osg]="OSG"
    [hdf5]="HDF5"
    [proj]="PROJ"
    [sqlite]="SQLite3"
)
declare -A DEP_ENVVARS=(
    [qt]="QT_ROOT"
    [osg]="OSG_ROOT"
    [hdf5]="HDF5_ROOT"
    [proj]="PROJ_ROOT"
    [sqlite]="SQLite3_ROOT"
)
declare -A DEP_PROMPTS=(
    [qt]="Enter your Qt install root, or leave blank for auto-detect."
    [osg]="Enter your OSG install root, or leave blank for auto-detect."
    [hdf5]="Enter your HDF5 install root, or leave blank for auto-detect."
    [proj]="Enter your PROJ install root, or leave blank for auto-detect."
    [sqlite]="Enter your SQLite3 install root, or leave blank for auto-detect."
)
declare -A DEP_INPUTS=()
declare -A DEP_INPUT_SOURCES=()
declare -A DEP_ROOTS=()
declare -A DEP_VERSIONS=()
declare -A DEP_STATUS=()
declare -A DEP_SOURCES=()

CONFIG=""
TARGETS_RAW="all"
JOBS=""
DO_INSTALL=0
INSTALL_PREFIX=""
NON_INTERACTIVE=0
ADVANCED=0
BUILD_DIR=""
AUTO_BUILD_DIR=1
CONFIGURE_DONE=0
CONFIGURE_DIRTY=1
USED_ARGUMENTS=0
GENERATOR=""
DEP_ANALYZED=0

usage() {
    cat <<'EOF'
Usage:
  bin/od_linux_build.sh
  bin/od_linux_build.sh [options]

Options:
  --config <Debug|Release|RelWithDebInfo|MinSizeRel>
  --targets <all|comma-separated-targets>
  --jobs <N>
  --install
  --install-prefix <PATH>
  --qt-root <PATH>
  --osg-root <PATH>
  --hdf5-root <PATH>
  --proj-root <PATH>
  --sqlite-root <PATH>
  --build-dir <PATH>
  --advanced
  --non-interactive
  --help

Examples:
  bin/od_linux_build.sh
  bin/od_linux_build.sh --config Release --targets od_main --jobs 8
  bin/od_linux_build.sh --config Debug --targets all --install \
    --install-prefix /opt/opendtect --non-interactive
EOF
}

die() {
    echo "Error: $*" >&2
    exit 1
}

trim() {
    local value="$1"
    value="${value#"${value%%[![:space:]]*}"}"
    value="${value%"${value##*[![:space:]]}"}"
    printf '%s' "$value"
}

canonicalize_existing_dir() {
    local path="$1"
    if [[ -d "$path" ]]; then
        ( cd -- "$path" && pwd )
    else
        printf '%s' "$path"
    fi
}

detect_jobs() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    elif command -v getconf >/dev/null 2>&1; then
        getconf _NPROCESSORS_ONLN
    else
        printf '1\n'
    fi
}

choose_generator() {
    if command -v ninja >/dev/null 2>&1; then
        printf 'Ninja\n'
    else
        printf 'Unix Makefiles\n'
    fi
}

require_linux() {
    local kernel
    kernel="$(uname -s)"
    if [[ "$kernel" != "Linux" ]]; then
        die "This helper is Linux-only. Detected platform: ${kernel}"
    fi
}

require_dialog() {
    if ! command -v dialog >/dev/null 2>&1; then
        die "Interactive mode requires the 'dialog' package. Install it and rerun."
    fi
}

require_ccmake() {
    if ! command -v ccmake >/dev/null 2>&1; then
        die "Advanced mode requires the 'ccmake' package. Install it and rerun."
    fi
}

dialog_input() {
    local title="$1"
    local prompt="$2"
    local initial="${3:-}"
    dialog --stdout --backtitle "$OD_BACKTITLE" --title "$title" \
        --inputbox "$prompt" 11 90 "$initial"
}

dialog_menu() {
    local title="$1"
    local prompt="$2"
    shift 2
    dialog --stdout --backtitle "$OD_BACKTITLE" --title "$title" \
        --menu "$prompt" 20 90 10 "$@"
}

dialog_yesno() {
    local title="$1"
    local prompt="$2"
    dialog --stdout --backtitle "$OD_BACKTITLE" --title "$title" \
        --yesno "$prompt" 14 90
}

dialog_message() {
    local title="$1"
    local message="$2"
    dialog --backtitle "$OD_BACKTITLE" --title "$title" \
        --msgbox "$message" 16 90
}

dialog_text() {
    local title="$1"
    local content="$2"
    local tmpfile
    tmpfile="$(mktemp)"
    printf '%s\n' "$content" > "$tmpfile"
    dialog --backtitle "$OD_BACKTITLE" --title "$title" \
        --textbox "$tmpfile" 22 100
    rm -f -- "$tmpfile"
}

mark_configure_dirty() {
    CONFIGURE_DIRTY=1
    if [[ "$AUTO_BUILD_DIR" -eq 1 ]]; then
        BUILD_DIR=""
        CONFIGURE_DONE=0
    fi
}

set_dep_input() {
    local key="$1"
    local value="$2"
    local source="$3"
    value="$(trim "$value")"
    if [[ -n "$value" ]]; then
        value="$(canonicalize_existing_dir "$value")"
        DEP_INPUTS["$key"]="$value"
        DEP_INPUT_SOURCES["$key"]="$source"
    fi
}

init_default_values() {
    JOBS="$(detect_jobs)"
    GENERATOR="$(choose_generator)"

    for key in "${DEP_KEYS[@]}"; do
        DEP_STATUS["$key"]="not checked"
        DEP_VERSIONS["$key"]="-"
        DEP_ROOTS["$key"]="-"
        DEP_SOURCES["$key"]="-"
    done
}

import_env_dependency_inputs() {
    local key envvar envval
    for key in "${DEP_KEYS[@]}"; do
        if [[ -n "${DEP_INPUTS[$key]:-}" ]]; then
            continue
        fi
        envvar="${DEP_ENVVARS[$key]}"
        envval="${!envvar:-}"
        if [[ -n "$envval" ]]; then
            set_dep_input "$key" "$envval" "env"
        fi
    done
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        USED_ARGUMENTS=1
        case "$1" in
            --config)
                [[ $# -ge 2 ]] || die "--config requires a value"
                CONFIG="$2"
                mark_configure_dirty
                shift 2
                ;;
            --targets)
                [[ $# -ge 2 ]] || die "--targets requires a value"
                TARGETS_RAW="$2"
                shift 2
                ;;
            --jobs)
                [[ $# -ge 2 ]] || die "--jobs requires a value"
                JOBS="$2"
                shift 2
                ;;
            --install)
                DO_INSTALL=1
                mark_configure_dirty
                shift
                ;;
            --install-prefix)
                [[ $# -ge 2 ]] || die "--install-prefix requires a value"
                INSTALL_PREFIX="$2"
                mark_configure_dirty
                shift 2
                ;;
            --qt-root)
                [[ $# -ge 2 ]] || die "--qt-root requires a value"
                set_dep_input qt "$2" "cli"
                mark_configure_dirty
                shift 2
                ;;
            --osg-root)
                [[ $# -ge 2 ]] || die "--osg-root requires a value"
                set_dep_input osg "$2" "cli"
                mark_configure_dirty
                shift 2
                ;;
            --hdf5-root)
                [[ $# -ge 2 ]] || die "--hdf5-root requires a value"
                set_dep_input hdf5 "$2" "cli"
                mark_configure_dirty
                shift 2
                ;;
            --proj-root)
                [[ $# -ge 2 ]] || die "--proj-root requires a value"
                set_dep_input proj "$2" "cli"
                mark_configure_dirty
                shift 2
                ;;
            --sqlite-root)
                [[ $# -ge 2 ]] || die "--sqlite-root requires a value"
                set_dep_input sqlite "$2" "cli"
                mark_configure_dirty
                shift 2
                ;;
            --build-dir)
                [[ $# -ge 2 ]] || die "--build-dir requires a value"
                BUILD_DIR="$2"
                AUTO_BUILD_DIR=0
                CONFIGURE_DONE=0
                CONFIGURE_DIRTY=1
                shift 2
                ;;
            --non-interactive)
                NON_INTERACTIVE=1
                shift
                ;;
            --advanced)
                ADVANCED=1
                shift
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            *)
                die "Unknown option: $1"
                ;;
        esac
    done
}

validate_config() {
    case "$CONFIG" in
        Debug|Release|RelWithDebInfo|MinSizeRel) ;;
        *)
            die "Unsupported build configuration: ${CONFIG}"
            ;;
    esac
}

validate_jobs() {
    if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
        die "--jobs expects a positive integer"
    fi
}

ensure_required_inputs() {
    if [[ -z "$CONFIG" ]]; then
        if [[ "$NON_INTERACTIVE" -eq 1 ]]; then
            die "--config is required in non-interactive mode"
        fi
        prompt_for_config
    fi

    validate_config
    validate_jobs

    if [[ "$DO_INSTALL" -eq 1 && -n "$INSTALL_PREFIX" ]]; then
        INSTALL_PREFIX="$(canonicalize_existing_dir "$INSTALL_PREFIX")"
    fi

    if [[ "$DO_INSTALL" -eq 1 && "$NON_INTERACTIVE" -eq 1 && -z "$INSTALL_PREFIX" ]]; then
        die "--install requires --install-prefix in non-interactive mode"
    fi
}

prompt_for_config() {
    require_dialog
    local choice
    if ! choice="$(dialog_menu "Build Configuration" \
        "Choose the build configuration." \
        Debug "Debug build" \
        Release "Release build" \
        RelWithDebInfo "Release with debug info" \
        MinSizeRel "Minimal size release")"; then
        exit 1
    fi
    CONFIG="$choice"
    mark_configure_dirty
}

prompt_for_targets() {
    require_dialog
    local value
    if ! value="$(dialog_input "Build Targets" \
        "Enter 'all' or a comma-separated target list." \
        "$TARGETS_RAW")"; then
        return
    fi
    TARGETS_RAW="${value:-all}"
}

prompt_for_install_options() {
    require_dialog
    if dialog_yesno "Install Step" \
        "Run 'cmake --install' after a successful build?"; then
        DO_INSTALL=1
        local value
        if value="$(dialog_input "Install Prefix" \
            "Enter the install prefix. Leave blank to keep CMake's default/current prefix." \
            "$INSTALL_PREFIX")"; then
            INSTALL_PREFIX="$(trim "$value")"
            mark_configure_dirty
        fi
    else
        DO_INSTALL=0
        mark_configure_dirty
    fi
}

prompt_for_missing_dependencies() {
    if [[ "$NON_INTERACTIVE" -eq 1 ]]; then
        return
    fi
    if [[ "$DEP_ANALYZED" -eq 1 ]]; then
        return
    fi

    require_dialog

    local key value
    for key in "${DEP_KEYS[@]}"; do
        if [[ -n "${DEP_INPUTS[$key]:-}" ]]; then
            continue
        fi
        if ! value="$(dialog_input "${DEP_LABELS[$key]} Root" \
            "${DEP_PROMPTS[$key]}" "")"; then
            continue
        fi
        value="$(trim "$value")"
        if [[ -n "$value" ]]; then
            set_dep_input "$key" "$value" "prompt"
            mark_configure_dirty
        fi
    done
}

standard_prefixes() {
    local seen=""
    local prefix
    for prefix in /usr /usr/local /opt "$HOME/.local" "$HOME/Qt" "$HOME"; do
        if [[ -d "$prefix" && ":${seen}:" != *":${prefix}:"* ]]; then
            printf '%s\n' "$prefix"
            seen="${seen}:${prefix}"
        fi
    done
}

find_standard_file() {
    local filename="$1"
    local maxdepth="${2:-6}"
    local prefix match
    while IFS= read -r prefix; do
        while IFS= read -r match; do
            printf '%s\n' "$match"
            return 0
        done < <(find "$prefix" -maxdepth "$maxdepth" -type f -name "$filename" 2>/dev/null | sort)
    done < <(standard_prefixes)
    return 1
}

extract_define_value() {
    local file="$1"
    local macro="$2"
    [[ -f "$file" ]] || return 1
    sed -n "s/^[[:space:]]*#define[[:space:]]\\+${macro}[[:space:]]\\+\"\\{0,1\\}\\([^\"[:space:]]\\+\\)\"\\{0,1\\}.*/\\1/p" "$file" | head -n 1
}

version_from_cmake_config() {
    local file="$1"
    [[ -f "$file" ]] || return 1
    sed -n 's/.*PACKAGE_VERSION[[:space:]]*"\([^"]\+\)".*/\1/p' "$file" | head -n 1
}

cache_value_from_file() {
    local file="$1"
    local key="$2"
    [[ -f "$file" ]] || return 1
    sed -n "s/^${key}:[^=]*=//p" "$file" | head -n 1
}

prefix_from_trailer() {
    local path="$1"
    local trailer="$2"
    if [[ "$path" == *"/${trailer}/"* ]]; then
        printf '%s\n' "${path%%/${trailer}/*}"
        return 0
    fi
    if [[ "$path" == *"/${trailer}" ]]; then
        printf '%s\n' "${path%/${trailer}}"
        return 0
    fi
    return 1
}

set_found_result() {
    local key="$1"
    local root="$2"
    local version="$3"
    local source="$4"
    DEP_STATUS["$key"]="found"
    DEP_ROOTS["$key"]="$(canonicalize_existing_dir "$root")"
    DEP_VERSIONS["$key"]="${version:-unknown}"
    DEP_SOURCES["$key"]="$source"
}

set_missing_result() {
    local key="$1"
    local status="$2"
    local source="${3:--}"
    DEP_STATUS["$key"]="$status"
    DEP_ROOTS["$key"]="-"
    DEP_VERSIONS["$key"]="-"
    DEP_SOURCES["$key"]="$source"
}

resolve_qt_from_root() {
    local root="$1"
    local version=""
    local cfg

    [[ -d "$root" ]] || return 1

    if [[ -x "$root/bin/qmake6" ]]; then
        version="$("$root/bin/qmake6" -query QT_VERSION 2>/dev/null || true)"
    elif [[ -x "$root/bin/qmake" ]]; then
        version="$("$root/bin/qmake" -query QT_VERSION 2>/dev/null || true)"
    elif [[ -f "$root/lib/cmake/Qt6/Qt6ConfigVersion.cmake" ]]; then
        version="$(version_from_cmake_config "$root/lib/cmake/Qt6/Qt6ConfigVersion.cmake" || true)"
    elif [[ -f "$root/lib64/cmake/Qt6/Qt6ConfigVersion.cmake" ]]; then
        version="$(version_from_cmake_config "$root/lib64/cmake/Qt6/Qt6ConfigVersion.cmake" || true)"
    fi

    printf '%s\n' "${version:-unknown}"
}

detect_qt() {
    local root source version cfg

    root="${DEP_INPUTS[qt]:-}"
    source="${DEP_INPUT_SOURCES[qt]:-}"
    if [[ -n "$root" ]]; then
        if [[ ! -d "$root" ]]; then
            set_missing_result qt "invalid path" "$source"
            return
        fi
        version="$(resolve_qt_from_root "$root")"
        set_found_result qt "$root" "$version" "$source"
        return
    fi

    if command -v qmake6 >/dev/null 2>&1; then
        root="$(qmake6 -query QT_INSTALL_PREFIX 2>/dev/null || true)"
        version="$(qmake6 -query QT_VERSION 2>/dev/null || true)"
        if [[ -n "$root" && -d "$root" ]]; then
            set_found_result qt "$root" "$version" "auto:qmake6"
            return
        fi
    fi

    if command -v qmake >/dev/null 2>&1; then
        root="$(qmake -query QT_INSTALL_PREFIX 2>/dev/null || true)"
        version="$(qmake -query QT_VERSION 2>/dev/null || true)"
        if [[ -n "$root" && -d "$root" ]]; then
            set_found_result qt "$root" "$version" "auto:qmake"
            return
        fi
    fi

    if command -v qtpaths6 >/dev/null 2>&1; then
        root="$(qtpaths6 --query QT_INSTALL_PREFIX 2>/dev/null || true)"
        if [[ -n "$root" && -d "$root" ]]; then
            version="$(resolve_qt_from_root "$root")"
            set_found_result qt "$root" "$version" "auto:qtpaths6"
            return
        fi
    fi

    cfg="$(find_standard_file "Qt6ConfigVersion.cmake" 7 || true)"
    if [[ -n "$cfg" ]]; then
        root="$(prefix_from_trailer "$cfg" "lib/cmake/Qt6" || true)"
        if [[ -z "$root" ]]; then
            root="$(prefix_from_trailer "$cfg" "lib64/cmake/Qt6" || true)"
        fi
        version="$(version_from_cmake_config "$cfg" || true)"
        if [[ -n "$root" && -d "$root" ]]; then
            set_found_result qt "$root" "$version" "auto:cmake-config"
            return
        fi
    fi

    set_missing_result qt "not found"
}

resolve_osg_from_root() {
    local root="$1"
    local version=""
    local header="$root/include/osg/Version"
    [[ -d "$root" ]] || return 1

    if [[ -x "$root/bin/osgversion" ]]; then
        version="$("$root/bin/osgversion" 2>/dev/null || true)"
    elif [[ -f "$header" ]]; then
        local major minor patch
        major="$(extract_define_value "$header" "OPENSCENEGRAPH_MAJOR_VERSION" || true)"
        minor="$(extract_define_value "$header" "OPENSCENEGRAPH_MINOR_VERSION" || true)"
        patch="$(extract_define_value "$header" "OPENSCENEGRAPH_PATCH_VERSION" || true)"
        if [[ -n "$major" && -n "$minor" && -n "$patch" ]]; then
            version="${major}.${minor}.${patch}"
        fi
    fi

    printf '%s\n' "${version:-unknown}"
}

detect_osg() {
    local root source version pkg cfg

    root="${DEP_INPUTS[osg]:-}"
    source="${DEP_INPUT_SOURCES[osg]:-}"
    if [[ -n "$root" ]]; then
        if [[ ! -d "$root" ]]; then
            set_missing_result osg "invalid path" "$source"
            return
        fi
        version="$(resolve_osg_from_root "$root")"
        set_found_result osg "$root" "$version" "$source"
        return
    fi

    if command -v osgversion >/dev/null 2>&1; then
        version="$(osgversion 2>/dev/null || true)"
        root="$(dirname -- "$(dirname -- "$(command -v osgversion)")")"
        if [[ -d "$root" ]]; then
            set_found_result osg "$root" "$version" "auto:osgversion"
            return
        fi
    fi

    if command -v pkg-config >/dev/null 2>&1; then
        for pkg in openscenegraph openscenegraph-osg osg; do
            if pkg-config --exists "$pkg" 2>/dev/null; then
                root="$(pkg-config --variable=prefix "$pkg" 2>/dev/null || true)"
                version="$(pkg-config --modversion "$pkg" 2>/dev/null || true)"
                if [[ -n "$root" && -d "$root" ]]; then
                    set_found_result osg "$root" "$version" "auto:pkg-config:${pkg}"
                    return
                fi
            fi
        done
    fi

    cfg="$(find_standard_file "OpenSceneGraphConfig.cmake" 7 || true)"
    if [[ -n "$cfg" ]]; then
        root="$(prefix_from_trailer "$cfg" "lib/cmake/OpenSceneGraph" || true)"
        if [[ -z "$root" ]]; then
            root="$(prefix_from_trailer "$cfg" "lib64/cmake/OpenSceneGraph" || true)"
        fi
        if [[ -z "$root" ]]; then
            root="$(dirname -- "$(dirname -- "$(dirname -- "$cfg")")")"
        fi
        version="$(resolve_osg_from_root "$root")"
        if [[ -d "$root" ]]; then
            set_found_result osg "$root" "$version" "auto:cmake-config"
            return
        fi
    fi

    set_missing_result osg "not found"
}

resolve_hdf5_from_root() {
    local root="$1"
    local version=""
    local header="$root/include/H5public.h"
    [[ -d "$root" ]] || return 1

    if [[ -x "$root/bin/h5cc" ]]; then
        version="$("$root/bin/h5cc" -showconfig 2>/dev/null | sed -n 's/^[[:space:]]*HDF5 Version:[[:space:]]*//p' | head -n 1)"
    elif [[ -f "$header" ]]; then
        local major minor release
        major="$(extract_define_value "$header" "H5_VERS_MAJOR" || true)"
        minor="$(extract_define_value "$header" "H5_VERS_MINOR" || true)"
        release="$(extract_define_value "$header" "H5_VERS_RELEASE" || true)"
        if [[ -n "$major" && -n "$minor" && -n "$release" ]]; then
            version="${major}.${minor}.${release}"
        fi
    fi

    printf '%s\n' "${version:-unknown}"
}

detect_hdf5() {
    local root source version cfg pkg

    root="${DEP_INPUTS[hdf5]:-}"
    source="${DEP_INPUT_SOURCES[hdf5]:-}"
    if [[ -n "$root" ]]; then
        if [[ ! -d "$root" ]]; then
            set_missing_result hdf5 "invalid path" "$source"
            return
        fi
        version="$(resolve_hdf5_from_root "$root")"
        set_found_result hdf5 "$root" "$version" "$source"
        return
    fi

    if command -v h5cc >/dev/null 2>&1; then
        version="$(h5cc -showconfig 2>/dev/null | sed -n 's/^[[:space:]]*HDF5 Version:[[:space:]]*//p' | head -n 1)"
        root="$(dirname -- "$(dirname -- "$(command -v h5cc)")")"
        if [[ -d "$root" ]]; then
            set_found_result hdf5 "$root" "$version" "auto:h5cc"
            return
        fi
    fi

    if command -v pkg-config >/dev/null 2>&1; then
        for pkg in hdf5 hdf5-cpp; do
            if pkg-config --exists "$pkg" 2>/dev/null; then
                root="$(pkg-config --variable=prefix "$pkg" 2>/dev/null || true)"
                version="$(pkg-config --modversion "$pkg" 2>/dev/null || true)"
                if [[ -n "$root" && -d "$root" ]]; then
                    set_found_result hdf5 "$root" "$version" "auto:pkg-config:${pkg}"
                    return
                fi
            fi
        done
    fi

    cfg="$(find_standard_file "hdf5-config.cmake" 7 || true)"
    if [[ -n "$cfg" ]]; then
        root="$(prefix_from_trailer "$cfg" "cmake/hdf5" || true)"
        if [[ -z "$root" ]]; then
            root="$(prefix_from_trailer "$cfg" "share/cmake/hdf5" || true)"
        fi
        if [[ -z "$root" ]]; then
            root="$(prefix_from_trailer "$cfg" "lib/cmake/hdf5" || true)"
        fi
        if [[ -z "$root" ]]; then
            root="$(prefix_from_trailer "$cfg" "lib64/cmake/hdf5" || true)"
        fi
        version="$(version_from_cmake_config "${cfg%/*}/hdf5-config-version.cmake" || true)"
        if [[ -z "$version" ]]; then
            version="$(resolve_hdf5_from_root "$root")"
        fi
        if [[ -n "$root" && -d "$root" ]]; then
            set_found_result hdf5 "$root" "$version" "auto:cmake-config"
            return
        fi
    fi

    set_missing_result hdf5 "not found"
}

resolve_proj_from_root() {
    local root="$1"
    local version=""
    local header="$root/include/proj.h"
    local cfg=""
    [[ -d "$root" ]] || return 1

    cfg="$root/lib/cmake/proj/proj-config-version.cmake"
    if [[ ! -f "$cfg" ]]; then
        cfg="$root/share/cmake/proj/proj-config-version.cmake"
    fi
    if [[ ! -f "$cfg" ]]; then
        cfg="$root/lib64/cmake/proj/proj-config-version.cmake"
    fi
    if [[ -f "$cfg" ]]; then
        version="$(version_from_cmake_config "$cfg" || true)"
    elif [[ -f "$header" ]]; then
        local major minor patch
        major="$(extract_define_value "$header" "PROJ_VERSION_MAJOR" || true)"
        minor="$(extract_define_value "$header" "PROJ_VERSION_MINOR" || true)"
        patch="$(extract_define_value "$header" "PROJ_VERSION_PATCH" || true)"
        if [[ -n "$major" && -n "$minor" && -n "$patch" ]]; then
            version="${major}.${minor}.${patch}"
        fi
    fi

    printf '%s\n' "${version:-unknown}"
}

detect_proj() {
    local root source version cfg

    root="${DEP_INPUTS[proj]:-}"
    source="${DEP_INPUT_SOURCES[proj]:-}"
    if [[ -n "$root" ]]; then
        if [[ ! -d "$root" ]]; then
            set_missing_result proj "invalid path" "$source"
            return
        fi
        version="$(resolve_proj_from_root "$root")"
        set_found_result proj "$root" "$version" "$source"
        return
    fi

    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists proj 2>/dev/null; then
        root="$(pkg-config --variable=prefix proj 2>/dev/null || true)"
        version="$(pkg-config --modversion proj 2>/dev/null || true)"
        if [[ -n "$root" && -d "$root" ]]; then
            set_found_result proj "$root" "$version" "auto:pkg-config:proj"
            return
        fi
    fi

    cfg="$(find_standard_file "proj-config.cmake" 7 || true)"
    if [[ -z "$cfg" ]]; then
        cfg="$(find_standard_file "PROJConfig.cmake" 7 || true)"
    fi
    if [[ -n "$cfg" ]]; then
        root="$(prefix_from_trailer "$cfg" "lib/cmake/proj" || true)"
        if [[ -z "$root" ]]; then
            root="$(prefix_from_trailer "$cfg" "share/cmake/proj" || true)"
        fi
        if [[ -z "$root" ]]; then
            root="$(prefix_from_trailer "$cfg" "lib64/cmake/proj" || true)"
        fi
        version="$(version_from_cmake_config "${cfg%/*}/proj-config-version.cmake" || true)"
        if [[ -z "$version" ]]; then
            version="$(resolve_proj_from_root "$root")"
        fi
        if [[ -n "$root" && -d "$root" ]]; then
            set_found_result proj "$root" "$version" "auto:cmake-config"
            return
        fi
    fi

    set_missing_result proj "not found"
}

resolve_sqlite_from_root() {
    local root="$1"
    local version=""
    local header="$root/include/sqlite3.h"
    local cfg=""
    [[ -d "$root" ]] || return 1

    cfg="$root/lib/cmake/SQLite3/SQLite3ConfigVersion.cmake"
    if [[ ! -f "$cfg" ]]; then
        cfg="$root/lib64/cmake/SQLite3/SQLite3ConfigVersion.cmake"
    fi
    if [[ ! -f "$cfg" ]]; then
        cfg="$root/share/cmake/SQLite3/SQLite3ConfigVersion.cmake"
    fi
    if [[ -f "$cfg" ]]; then
        version="$(version_from_cmake_config "$cfg" || true)"
    elif [[ -f "$header" ]]; then
        version="$(extract_define_value "$header" "SQLITE_VERSION" || true)"
    fi

    printf '%s\n' "${version:-unknown}"
}

detect_sqlite() {
    local root source version cfg

    root="${DEP_INPUTS[sqlite]:-}"
    source="${DEP_INPUT_SOURCES[sqlite]:-}"
    if [[ -n "$root" ]]; then
        if [[ ! -d "$root" ]]; then
            set_missing_result sqlite "invalid path" "$source"
            return
        fi
        version="$(resolve_sqlite_from_root "$root")"
        set_found_result sqlite "$root" "$version" "$source"
        return
    fi

    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sqlite3 2>/dev/null; then
        root="$(pkg-config --variable=prefix sqlite3 2>/dev/null || true)"
        version="$(pkg-config --modversion sqlite3 2>/dev/null || true)"
        if [[ -n "$root" && -d "$root" ]]; then
            set_found_result sqlite "$root" "$version" "auto:pkg-config:sqlite3"
            return
        fi
    fi

    if command -v sqlite3 >/dev/null 2>&1; then
        version="$(sqlite3 --version 2>/dev/null | awk '{print $1}' || true)"
        root="$(dirname -- "$(dirname -- "$(command -v sqlite3)")")"
        if [[ -n "$root" && -d "$root" ]]; then
            set_found_result sqlite "$root" "$version" "auto:sqlite3"
            return
        fi
    fi

    cfg="$(find_standard_file "SQLite3Config.cmake" 7 || true)"
    if [[ -z "$cfg" ]]; then
        cfg="$(find_standard_file "SQLite3ConfigVersion.cmake" 7 || true)"
    fi
    if [[ -n "$cfg" ]]; then
        root="$(prefix_from_trailer "$cfg" "lib/cmake/SQLite3" || true)"
        if [[ -z "$root" ]]; then
            root="$(prefix_from_trailer "$cfg" "lib64/cmake/SQLite3" || true)"
        fi
        if [[ -z "$root" ]]; then
            root="$(prefix_from_trailer "$cfg" "share/cmake/SQLite3" || true)"
        fi
        version="$(version_from_cmake_config "$cfg" || true)"
        if [[ -z "$version" ]]; then
            version="$(resolve_sqlite_from_root "$root")"
        fi
        if [[ -n "$root" && -d "$root" ]]; then
            set_found_result sqlite "$root" "$version" "auto:cmake-config"
            return
        fi
    fi

    set_missing_result sqlite "not found"
}

analyze_dependencies() {
    prompt_for_missing_dependencies
    detect_qt
    detect_osg
    detect_hdf5
    detect_proj
    detect_sqlite
    DEP_ANALYZED=1
}

dependency_summary() {
    local key
    printf 'Dependency summary\n'
    printf '==================\n'
    for key in "${DEP_KEYS[@]}"; do
        printf '%s\n' "${DEP_LABELS[$key]}:"
        printf '  status: %s\n' "${DEP_STATUS[$key]}"
        printf '  root:   %s\n' "${DEP_ROOTS[$key]}"
        printf '  version: %s\n' "${DEP_VERSIONS[$key]}"
        printf '  source: %s\n' "${DEP_SOURCES[$key]}"
    done
}

cache_value() {
    local key="$1"
    local cache_file="${BUILD_DIR}/CMakeCache.txt"
    [[ -f "$cache_file" ]] || return 1
    sed -n "s/^${key}:[^=]*=//p" "$cache_file" | head -n 1
}

configured_summary() {
    local build_type install_prefix qt_dir osg_root hdf5_dir proj_dir sqlite_version osg_version sqlite_display
    build_type="$(cache_value CMAKE_BUILD_TYPE || true)"
    install_prefix="$(cache_value CMAKE_INSTALL_PREFIX || true)"
    qt_dir="$(cache_value Qt6_DIR || true)"
    if [[ -z "$qt_dir" ]]; then
        qt_dir="$(cache_value QT_DIR || true)"
    fi
    osg_root="$(cache_value OSG_ROOT || true)"
    hdf5_dir="$(cache_value HDF5_DIR || true)"
    if [[ -z "$hdf5_dir" ]]; then
        hdf5_dir="$(cache_value HDF5_ROOT || true)"
    fi
    proj_dir="$(cache_value PROJ_DIR || true)"
    sqlite_version="$(cache_value SQLite3_VERSION || true)"
    osg_version="$(cache_value OPENSCENEGRAPH_VERSION || true)"
    sqlite_display="${sqlite_version:-<not cached>}"

    cat <<EOF
Configured build
================
Build dir:      ${BUILD_DIR}
Generator:      ${GENERATOR}
Build type:     ${build_type:-${CONFIG}}
Install prefix: ${install_prefix:-<default>}
Qt6_DIR:        ${qt_dir:-<not cached>}
OSG_ROOT:       ${osg_root:-<not cached>}
OSG version:    ${osg_version:-<not cached>}
HDF5_DIR/ROOT:  ${hdf5_dir:-<not cached>}
PROJ_DIR:       ${proj_dir:-<not cached>}
SQLite3_VERSION: ${sqlite_display}
EOF
}

current_status_text() {
    local install_state install_display
    install_state="disabled"
    if [[ "$DO_INSTALL" -eq 1 ]]; then
        install_state="enabled"
    fi

    if [[ -n "$INSTALL_PREFIX" ]]; then
        install_display="$INSTALL_PREFIX"
    else
        install_display="<default>"
    fi

    cat <<EOF
Config: ${CONFIG:-<unset>}
Targets: ${TARGETS_RAW}
Jobs: ${JOBS}
Generator: ${GENERATOR}
Install: ${install_state}
Prefix: ${install_display}
Build dir: ${BUILD_DIR:-<auto>}
Dependencies: $([[ "$DEP_ANALYZED" -eq 1 ]] && printf 'analyzed' || printf 'pending')
EOF
}

normalize_targets() {
    local trimmed
    TARGETS_RAW="$(trim "$TARGETS_RAW")"
    if [[ -z "$TARGETS_RAW" ]]; then
        TARGETS_RAW="all"
    fi
}

assign_build_dir_if_needed() {
    if [[ -n "$BUILD_DIR" ]]; then
        BUILD_DIR="$(canonicalize_existing_dir "$BUILD_DIR")"
        if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
            GENERATOR="$(cache_value_from_file "${BUILD_DIR}/CMakeCache.txt" CMAKE_GENERATOR || printf '%s' "$GENERATOR")"
        fi
        return
    fi

    local stamp config_slug
    config_slug="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
    stamp="$(date +%Y%m%d_%H%M%S)"
    BUILD_DIR="${OD_BUILD_ROOT}/${config_slug}/${stamp}"
}

run_cmd() {
    printf '\n$'
    printf ' %q' "$@"
    printf '\n'
    "$@"
}

configure_build() {
    ensure_required_inputs
    analyze_dependencies
    assign_build_dir_if_needed

    mkdir -p -- "$BUILD_DIR"

    local -a cmd
    cmd=(
        cmake
        -S "$OD_SOURCE_DIR"
        -B "$BUILD_DIR"
        -G "$GENERATOR"
        -DCMAKE_BUILD_TYPE="$CONFIG"
    )

    if [[ -n "$INSTALL_PREFIX" ]]; then
        cmd+=("-DCMAKE_INSTALL_PREFIX=$(canonicalize_existing_dir "$INSTALL_PREFIX")")
    fi
    if [[ "${DEP_STATUS[qt]}" == "found" ]]; then
        cmd+=("-DQT_ROOT=${DEP_ROOTS[qt]}")
    fi
    if [[ "${DEP_STATUS[osg]}" == "found" ]]; then
        cmd+=("-DOSG_ROOT=${DEP_ROOTS[osg]}")
    fi
    if [[ "${DEP_STATUS[hdf5]}" == "found" ]]; then
        cmd+=("-DHDF5_ROOT=${DEP_ROOTS[hdf5]}")
    fi
    if [[ "${DEP_STATUS[proj]}" == "found" ]]; then
        cmd+=("-DPROJ_ROOT=${DEP_ROOTS[proj]}")
    fi
    if [[ "${DEP_STATUS[sqlite]}" == "found" ]]; then
        cmd+=("-DSQLite3_ROOT=${DEP_ROOTS[sqlite]}")
    fi

    run_cmd "${cmd[@]}"
    CONFIGURE_DONE=1
    CONFIGURE_DIRTY=0
}

open_advanced_cache_editor() {
    require_ccmake
    if [[ "$CONFIGURE_DONE" -eq 0 || "$CONFIGURE_DIRTY" -eq 1 ]]; then
        configure_build
    fi
    printf '\nOpening ccmake for %s\n' "$BUILD_DIR"
    ccmake "$BUILD_DIR"
    CONFIGURE_DONE=1
    CONFIGURE_DIRTY=0
}

validate_target() {
    local target="$1"
    local -a cmd
    cmd=(cmake --build "$BUILD_DIR" --target "$target" -- -n)
    "${cmd[@]}" >/dev/null 2>&1
}

parse_target_array() {
    local token
    BUILD_TARGETS=()
    normalize_targets
    if [[ "$TARGETS_RAW" == "all" ]]; then
        return
    fi
    IFS=',' read -r -a BUILD_TARGETS <<< "$TARGETS_RAW"
    local i trimmed
    for i in "${!BUILD_TARGETS[@]}"; do
        trimmed="$(trim "${BUILD_TARGETS[$i]}")"
        BUILD_TARGETS[$i]="$trimmed"
    done
    local -a filtered=()
    for token in "${BUILD_TARGETS[@]}"; do
        if [[ -n "$token" ]]; then
            filtered+=("$token")
        fi
    done
    BUILD_TARGETS=("${filtered[@]}")
    if [[ ${#BUILD_TARGETS[@]} -eq 0 ]]; then
        die "No build targets were specified"
    fi
}

validate_targets() {
    local target
    parse_target_array
    if [[ ${#BUILD_TARGETS[@]} -eq 0 ]]; then
        return
    fi
    for target in "${BUILD_TARGETS[@]}"; do
        if ! validate_target "$target"; then
            die "Target '${target}' is not available in ${BUILD_DIR}. Try 'all' or inspect 'cmake --build ${BUILD_DIR} --target help'."
        fi
    done
}

collect_install_inputs() {
    local script line candidate remainder
    while IFS= read -r -d '' script; do
        while IFS= read -r line; do
            remainder="$line"
            while [[ "$remainder" =~ \"([^\"]+)\" ]]; do
                candidate="${BASH_REMATCH[1]}"
                if [[ "$candidate" == "${BUILD_DIR}/"* ]]; then
                    printf '%s\n' "$candidate"
                fi
                remainder="${remainder#*"${BASH_REMATCH[0]}"}"
            done
        done < "$script"
    done < <(find "$BUILD_DIR" -name cmake_install.cmake -print0)
}

check_install_inputs() {
    local -a missing=()
    local candidate

    if [[ ! -f "${BUILD_DIR}/cmake_install.cmake" ]]; then
        die "Install scripts were not generated in ${BUILD_DIR}. Configure first."
    fi

    while IFS= read -r candidate; do
        [[ -n "$candidate" ]] || continue
        if [[ "$candidate" == *'${'* ]]; then
            continue
        fi
        if [[ ! -e "$candidate" ]]; then
            missing+=("$candidate")
        fi
    done < <(collect_install_inputs | sort -u)

    if [[ ${#missing[@]} -gt 0 ]]; then
        echo
        echo "Install safety check failed."
        echo "The generated install scripts reference build artifacts that are not present."
        echo "This usually means the selected target list is smaller than what this repo's install step expects."
        echo
        echo "Missing artifacts (showing up to 10):"
        printf '  - %s\n' "${missing[@]:0:10}"
        echo
        echo "Try rerunning with '--targets all' for installable runs, or rerun without '--install'."
        return 1
    fi
}

run_build() {
    local -a cmd
    if [[ "$CONFIGURE_DONE" -eq 0 || "$CONFIGURE_DIRTY" -eq 1 ]]; then
        configure_build
    fi

    validate_targets

    cmd=(cmake --build "$BUILD_DIR" --parallel "$JOBS")
    if [[ ${#BUILD_TARGETS[@]} -gt 0 ]]; then
        cmd+=(--target "${BUILD_TARGETS[@]}")
    fi
    run_cmd "${cmd[@]}"
}

run_install() {
    if [[ "$DO_INSTALL" -ne 1 ]]; then
        return
    fi

    check_install_inputs

    local -a cmd
    cmd=(cmake --install "$BUILD_DIR")
    if [[ -n "$INSTALL_PREFIX" ]]; then
        cmd+=(--prefix "$INSTALL_PREFIX")
    fi
    run_cmd "${cmd[@]}"
}

review_summary() {
    cat <<EOF
Ready to run
============
Build dir: ${BUILD_DIR:-<auto>}
Generator: ${GENERATOR}
Config: ${CONFIG}
Targets: ${TARGETS_RAW}
Jobs: ${JOBS}
Install: $([[ "$DO_INSTALL" -eq 1 ]] && printf 'yes' || printf 'no')
Install prefix: ${INSTALL_PREFIX:-<default>}

Dependency status:
- Qt: ${DEP_STATUS[qt]} (${DEP_VERSIONS[qt]})
- OSG: ${DEP_STATUS[osg]} (${DEP_VERSIONS[osg]})
- HDF5: ${DEP_STATUS[hdf5]} (${DEP_VERSIONS[hdf5]})
- PROJ: ${DEP_STATUS[proj]} (${DEP_VERSIONS[proj]})
- SQLite3: ${DEP_STATUS[sqlite]} (${DEP_VERSIONS[sqlite]})
EOF
}

run_workflow() {
    configure_build
    if [[ "$ADVANCED" -eq 1 ]]; then
        open_advanced_cache_editor
        ADVANCED=0
    fi
    run_build
    run_install
}

interactive_menu() {
    require_dialog
    local choice

    while true; do
        if ! choice="$(dialog_menu "Main Menu" "$(current_status_text)" \
            analyze "Analyze Dependencies" \
            configure "Configure Build" \
            targets "Choose Targets" \
            install "Install Options" \
            review "Review and Run" \
            advanced "Advanced Cache Editor" \
            exit "Exit")"; then
            exit 0
        fi

        case "$choice" in
            analyze)
                analyze_dependencies
                dialog_text "Dependency Summary" "$(dependency_summary)"
                ;;
            configure)
                ensure_required_inputs
                configure_build
                dialog_text "Configured Build" "$(configured_summary)"
                ;;
            targets)
                prompt_for_targets
                ;;
            install)
                prompt_for_install_options
                ;;
            review)
                ensure_required_inputs
                analyze_dependencies
                assign_build_dir_if_needed
                if dialog_yesno "Review and Run" "$(review_summary)"; then
                    clear
                    run_workflow
                    exit 0
                fi
                ;;
            advanced)
                ensure_required_inputs
                clear
                open_advanced_cache_editor
                ;;
            exit)
                exit 0
                ;;
        esac
    done
}

main() {
    require_linux
    init_default_values
    parse_args "$@"
    import_env_dependency_inputs

    if [[ "$USED_ARGUMENTS" -eq 0 && "$NON_INTERACTIVE" -eq 0 && -t 0 && -t 1 ]]; then
        interactive_menu
        exit 0
    fi

    ensure_required_inputs
    analyze_dependencies

    echo
    dependency_summary
    echo

    run_workflow

    echo
    configured_summary
}

main "$@"
