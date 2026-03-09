#!/usr/bin/env bash

# Rorschach-LED Screensaver
# Generative symmetrical inkblot patterns
#
# This launcher will attempt to compile and run a high-performance C version.
# If no C compiler is found, it falls back to the original Bash animation.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
C_SOURCE="$SCRIPT_DIR/rorschach-led.c"
C_BINARY="$SCRIPT_DIR/rorschach-led.bin"

# ─── Try to run the C version ──────────────────────────────────────────
try_native() {
    # If C source exists, try to compile & run the native binary
    if [[ ! -f "$C_SOURCE" ]]; then
        return 1
    fi

    # Compile if binary is missing or source is newer
    if [[ ! -x "$C_BINARY" ]] || [[ "$C_SOURCE" -nt "$C_BINARY" ]]; then
        local compiler=""
        for cc in cc clang gcc; do
            if command -v "$cc" &>/dev/null; then
                compiler="$cc"
                break
            fi
        done
        if [[ -z "$compiler" ]]; then
            return 1  # No compiler found, fall back to bash
        fi
        # Compile silently
        "$compiler" -O3 -o "$C_BINARY" "$C_SOURCE" -lm 2>/dev/null || return 1
    fi

    # Run the native binary (inherits SCREENSAVER_DELAY from environment)
    exec "$C_BINARY"
}

# Attempt native version first
try_native

# ─── Bash fallback ─────────────────────────────────────────────────────
# If we reach here, the C version couldn't be compiled or run.
# Fall back to the original pure-bash implementation.

_cleanup_and_exit() {
  tput cnorm; tput sgr0; echo; exit 0
}
trap _cleanup_and_exit EXIT INT TERM QUIT

# Use a square grid to support diagonal symmetry
GRID_W=10
GRID_H=10
declare -a NOISE_GRID
declare -a TARGET_GRID

# Symmetry Mode
# 0 = Vertical (Rorschach Classic)
# 1 = Horizontal (Water Reflection)
# 2 = Diagonal (Cross)
# 3 = Quad (Vertical + Horizontal)
# 4 = Kaleidoscope (Vertical + Horizontal + Diagonal)
SYM_MODE=0

init_symmetry() {
    SYM_MODE=$((RANDOM % 5))
}

symmetrize_grid() {
    local y x source_idx target_idx half_w half_h
    # Helper to copy Upper-Right Triangle to Lower-Left Triangle (Diagonal Mirror)
    if [[ $SYM_MODE -eq 2 || $SYM_MODE -eq 4 ]]; then
        for ((y=0; y<GRID_H; y++)); do
            for ((x=y+1; x<GRID_W; x++)); do
                source_idx=$((y * GRID_W + x))
                target_idx=$((x * GRID_W + y))
                NOISE_GRID[target_idx]=${NOISE_GRID[source_idx]}
                TARGET_GRID[target_idx]=${TARGET_GRID[source_idx]}
            done
        done
    fi

    # Helper to copy Left Half to Right Half (Vertical Mirror)
    if [[ $SYM_MODE -eq 0 || $SYM_MODE -eq 3 || $SYM_MODE -eq 4 ]]; then
        half_w=$((GRID_W / 2))
        for ((y=0; y<GRID_H; y++)); do
            for ((x=0; x<half_w; x++)); do
                source_idx=$((y * GRID_W + x))
                target_idx=$((y * GRID_W + (GRID_W - 1 - x)))
                NOISE_GRID[target_idx]=${NOISE_GRID[source_idx]}
                TARGET_GRID[target_idx]=${TARGET_GRID[source_idx]}
            done
        done
    fi

    # Helper to copy Top Half to Bottom Half (Horizontal Mirror)
    if [[ $SYM_MODE -eq 1 || $SYM_MODE -eq 3 || $SYM_MODE -eq 4 ]]; then
        half_h=$((GRID_H / 2))
        for ((y=0; y<half_h; y++)); do
            for ((x=0; x<GRID_W; x++)); do
                source_idx=$((y * GRID_W + x))
                target_idx=$(((GRID_H - 1 - y) * GRID_W + x))
                NOISE_GRID[target_idx]=${NOISE_GRID[source_idx]}
                TARGET_GRID[target_idx]=${TARGET_GRID[source_idx]}
            done
        done
    fi
}

# Init
init_symmetry
for ((i=0; i<GRID_W*GRID_H; i++)); do
    NOISE_GRID[i]=$((RANDOM % 256))
    TARGET_GRID[i]=$((RANDOM % 256))
done
symmetrize_grid

update_grid() {
    local i current target diff step
    for ((i=0; i<GRID_W*GRID_H; i++)); do
        current=${NOISE_GRID[i]}
        target=${TARGET_GRID[i]}

        # Calculate difference
        diff=$((target - current))

        if (( diff == 0 )); then
            # Reached target, pick new one
            TARGET_GRID[i]=$((RANDOM % 256))
        else
            # Move towards target with easing
            # Faster speed: divide by 8 instead of 16
            step=$(( diff / 8 ))

            # Ensure minimum movement of 1 to prevent stalling
            if (( step == 0 )); then
                if (( diff > 0 )); then step=1; else step=-1; fi
            fi

            NOISE_GRID[i]=$(( current + step ))
        fi
    done

    # Enforce symmetry after updates
    symmetrize_grid
}

animate() {
    tput civis
    local width=$(tput cols)
    local height=$(tput lines)
    local delay=${SCREENSAVER_DELAY:-0.033}

    # Speed Optimization: Render at half horizontal resolution
    local calc_width=$((width / 2))
    if ((calc_width < 1)); then calc_width=1; fi

    local gw_minus_1=$((GRID_W - 1))
    local gh_minus_1=$((GRID_H - 1))

    # Palette: Transparent + 4 shades + Orange Accent
    local palette=(0 237 242 247 252 208)

    # -------------------------------------------------------------
    # Memory optimization: declare loop variables OUTSIDE the loop
    # to avoid Bash 3.2 memory leak from re-creating 'local' inside
    # tight loops.
    # -------------------------------------------------------------
    local frame_buffer y y_scaled gy ry row_idx_1 row_idx_2
    local gx current_x x_scaled rx v_left v_right val noise p_idx color
    local v1 v2

    clear # Clear screen initially

    while true; do
        update_grid
        frame_buffer="\e[H"

        for ((y=0; y<height; y++)); do
            # Vertical interpolation logic
            y_scaled=$(( y * gh_minus_1 * 1000 / height ))
            gy=$(( y_scaled / 1000 ))
            ry=$(( y_scaled % 1000 ))
            row_idx_1=$(( gy * GRID_W ))
            row_idx_2=$(( (gy + 1) * GRID_W ))

            color=0 # Default color index (Transparent)

            # Correct Horizontal Interpolation
            for ((current_x=0; current_x<calc_width; current_x++)); do
                 x_scaled=$(( current_x * gw_minus_1 * 1000 / calc_width ))
                 gx=$(( x_scaled / 1000 ))
                 rx=$(( x_scaled % 1000 ))

                 # Omit temporary array creation, calculate left/right vals inline
                 v1=${NOISE_GRID[row_idx_1 + gx]}
                 v2=${NOISE_GRID[row_idx_2 + gx]}
                 v_left=$(( v1 + (v2 - v1) * ry / 1000 ))

                 v1=${NOISE_GRID[row_idx_1 + gx + 1]}
                 v2=${NOISE_GRID[row_idx_2 + gx + 1]}
                 v_right=$(( v1 + (v2 - v1) * ry / 1000 ))

                 # Interpolate
                 val=$(( v_left + (v_right - v_left) * rx / 1000 ))

                 # Gaussian Noise
                 if (( RANDOM % 2 == 0 )); then
                    noise=$(( (RANDOM % 17) - 8 ))
                    val=$(( val + noise ))
                 fi

                 # Clamp value
                 if (( val < 0 )); then val=0; fi
                 if (( val > 255 )); then val=255; fi

                 p_idx=0

                 if (( val < 50 )); then
                    p_idx=0 # Transparent
                 elif (( val > 230 )); then
                    # Accent
                    if (( (RANDOM % 10) > 5 )); then
                        p_idx=5 # Orange
                    else
                        p_idx=4 # Brightest Gray
                    fi
                 else
                    p_idx=$(( (val - 50) * 4 / 190 + 1 ))
                    if ((p_idx > 4)); then p_idx=4; fi
                    if ((p_idx < 1)); then p_idx=1; fi
                 fi

                 # Render
                 if (( p_idx == 0 )); then
                    frame_buffer+="\e[0m  "
                 else
                    color=${palette[$p_idx]}
                    frame_buffer+="\e[48;5;${color}m  "
                 fi
            done

            if (( width % 2 != 0 )); then
                if (( p_idx == 0 )); then
                    frame_buffer+="\e[0m "
                else
                    frame_buffer+="\e[48;5;${color}m "
                fi
            fi

            frame_buffer+="\e[0m"
            if ((y < height - 1)); then frame_buffer+="\n"; fi
        done

        printf '%b' "$frame_buffer"
        sleep "$delay"
    done
}

animate
