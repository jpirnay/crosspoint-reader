import sys
import argparse
import re
import threading
from datetime import datetime
from collections import deque

# Try to import potentially missing packages
PACKAGE_MAPPING = {
    "serial": "pyserial",
    "colorama": "colorama",
    "matplotlib": "matplotlib",
}

try:
    import serial
    from colorama import init, Fore, Style
    import matplotlib.pyplot as plt
    import matplotlib.animation as animation
except ImportError as e:
    error_msg = str(e).lower()
    missing_packages = [pkg for mod, pkg in PACKAGE_MAPPING.items() if mod in error_msg]

    if not missing_packages:
        # Fallback if mapping doesn't cover
        missing_packages = ["pyserial", "colorama", "matplotlib"]

    print("\n" + "!" * 50)
    print(f" Error: Required package(s) not installed: {', '.join(missing_packages)}")
    print("!" * 50)

    print("\nTo fix this, please run the following command in your terminal:\n")
    install_cmd = "pip install " if sys.platform.startswith("win") else "pip3 install "
    print(f"    {install_cmd}{' '.join(missing_packages)}")

    print("\nExiting...")
    sys.exit(1)

# --- Global Variables for Data Sharing ---
# Store last 50 data points
MAX_POINTS = 50
time_data = deque(maxlen=MAX_POINTS)
free_mem_data = deque(maxlen=MAX_POINTS)
total_mem_data = deque(maxlen=MAX_POINTS)
data_lock = threading.Lock()  # Prevent reading while writing

# Initialize colors
init(autoreset=True)

# Color mapping for log lines
COLOR_KEYWORDS = {
    Fore.RED: ["ERROR", "[ERR]", "[SCT]", "FAILED", "WARNING"],
    Fore.CYAN: ["[MEM]", "FREE:"],
    Fore.MAGENTA: [
        "[GFX]",
        "[ERS]",
        "DISPLAY",
        "RAM WRITE",
        "RAM COMPLETE",
        "REFRESH",
        "POWERING ON",
        "FRAME BUFFER",
        "LUT",
    ],
    Fore.GREEN: [
        "[EBP]",
        "[BMC]",
        "[ZIP]",
        "[PARSER]",
        "[EHP]",
        "LOADING EPUB",
        "CACHE",
        "DECOMPRESSED",
        "PARSING",
    ],
    Fore.YELLOW: ["[ACT]", "ENTERING ACTIVITY", "EXITING ACTIVITY"],
    Fore.BLUE: ["RENDERED PAGE", "[LOOP]", "DURATION", "WAIT COMPLETE"],
    Fore.LIGHTYELLOW_EX: [
        "[CPS]",
        "SETTINGS",
        "[CLEAR_CACHE]",
        "[CHAP]",
        "[OPDS]",
        "[COF]",
    ],
    Fore.LIGHTBLACK_EX: [
        "ESP-ROM",
        "BUILD:",
        "RST:",
        "BOOT:",
        "SPIWP:",
        "MODE:",
        "LOAD:",
        "ENTRY",
        "[SD]",
        "STARTING CROSSPOINT",
        "VERSION",
    ],
    Fore.LIGHTCYAN_EX: ["[RBS]"],
    Fore.LIGHTMAGENTA_EX: [
        "[KRS]",
        "EINKDISPLAY:",
        "STATIC FRAME",
        "INITIALIZING",
        "SPI INITIALIZED",
        "GPIO PINS",
        "RESETTING",
        "SSD1677",
        "E-INK",
    ],
    Fore.LIGHTGREEN_EX: ["[FNS]", "FOOTNOTE"],
}


def get_color_for_line(line):
    """
    Classify log lines by type and assign appropriate colors.
    """
    line_upper = line.upper()
    for color, keywords in COLOR_KEYWORDS.items():
        if any(keyword in line_upper for keyword in keywords):
            return color
    return Fore.WHITE


def parse_memory_line(line):
    """
    Extracts Free and Total bytes from the specific log line.
    Format: [MEM] Free: 196344 bytes, Total: 226412 bytes, Min Free: 112620 bytes
    """
    # Regex to find 'Free: <digits>' and 'Total: <digits>'
    match = re.search(r"Free:\s*(\d+).*Total:\s*(\d+)", line)
    if match:
        try:
            free_bytes = int(match.group(1))
            total_bytes = int(match.group(2))
            return free_bytes, total_bytes
        except ValueError:
            return None, None
    return None, None


def serial_worker(port, baud, kwargs):
    """
    Runs in a background thread. Handles reading serial, printing to console,
    and updating the data lists.
    """
    print(f"{Fore.CYAN}--- Opening {port} at {baud} baud ---{Style.RESET_ALL}")
    filter = kwargs.get("filter", "").lower()
    suppress = kwargs.get("suppress", "").lower()
    if filter and suppress and filter == suppress:
        print(
            f"{Fore.YELLOW}Warning: Filter and Suppress keywords are the same. This may result in no output.{Style.RESET_ALL}"
        )
    if filter:
        print(
            f"{Fore.YELLOW}Filtering lines to only show those containing: '{filter}'{Style.RESET_ALL}"
        )
    if suppress:
        print(
            f"{Fore.YELLOW}Suppressing lines containing: '{suppress}'{Style.RESET_ALL}"
        )

    try:
        ser = serial.Serial(port, baud, timeout=0.1)
        ser.dtr = False
        ser.rts = False
    except serial.SerialException as e:
        print(f"{Fore.RED}Error opening port: {e}{Style.RESET_ALL}")
        return

    try:
        while True:
            try:
                raw_data = ser.readline().decode("utf-8", errors="replace")

                if not raw_data:
                    continue

                clean_line = raw_data.strip()
                if not clean_line:
                    continue

                # Add PC timestamp
                pc_time = datetime.now().strftime("%H:%M:%S")
                formatted_line = re.sub(r"^\[\d+\]", f"[{pc_time}]", clean_line)

                # Check for Memory Line
                if "[MEM]" in formatted_line:
                    free_val, total_val = parse_memory_line(formatted_line)
                    if free_val is not None:
                        with data_lock:
                            time_data.append(pc_time)
                            free_mem_data.append(free_val / 1024)  # Convert to KB
                            total_mem_data.append(total_val / 1024)  # Convert to KB
                # Apply filters
                if filter and filter not in formatted_line.lower():
                    continue
                if suppress and suppress in formatted_line.lower():
                    continue
                # Print to console
                line_color = get_color_for_line(formatted_line)
                print(f"{line_color}{formatted_line}")

            except OSError:
                print(f"{Fore.RED}Device disconnected.{Style.RESET_ALL}")
                break
    except Exception:
        # If thread is killed violently (e.g. main exit), silence errors
        pass
    finally:
        if "ser" in locals() and ser.is_open:
            ser.close()


def update_graph(frame):
    """
    Called by Matplotlib animation to redraw the chart.
    """
    with data_lock:
        if not time_data:
            return

        # Convert deques to lists for plotting
        x = list(time_data)
        y_free = list(free_mem_data)
        y_total = list(total_mem_data)

    plt.cla()  # Clear axis

    # Plot Total RAM
    plt.plot(x, y_total, label="Total RAM (KB)", color="red", linestyle="--")

    # Plot Free RAM
    plt.plot(x, y_free, label="Free RAM (KB)", color="green", marker="o")

    # Fill area under Free RAM
    plt.fill_between(x, y_free, color="green", alpha=0.1)

    plt.title("ESP32 Memory Monitor")
    plt.ylabel("Memory (KB)")
    plt.xlabel("Time")
    plt.legend(loc="upper left")
    plt.grid(True, linestyle=":", alpha=0.6)

    # Rotate date labels
    plt.xticks(rotation=45, ha="right")
    plt.tight_layout()


def main():
    parser = argparse.ArgumentParser(description="ESP32 Monitor with Graph")
    if sys.platform.startswith("win"):
        default_port = "COM8"
    elif sys.platform.startswith("darwin"):
        default_port = "/dev/tty.usbmodem14101"
    else:
        default_port = "/dev/ttyACM0"
    parser.add_argument("port", nargs="?", default=default_port, help="Serial port")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument(
        "--filter",
        type=str,
        default="",
        help="Only display lines containing this keyword (case-insensitive)",
    )
    parser.add_argument(
        "--suppress",
        type=str,
        default="",
        help="Suppress lines containing this keyword (case-insensitive)",
    )
    args = parser.parse_args()

    # 1. Start the Serial Reader in a separate thread
    # Daemon=True means this thread dies when the main program closes
    myargs = vars(args)  # Convert Namespace to dict for easier passing
    a = {k: v for k, v in myargs.items()}  # all args
    t = threading.Thread(
        target=serial_worker, args=(args.port, args.baud, a), daemon=True
    )
    t.start()

    # 2. Set up the Graph (Main Thread)
    try:
        plt.style.use("light_background")
    except Exception:
        pass

    fig = plt.figure(figsize=(10, 6))

    # Update graph every 1000ms
    ani = animation.FuncAnimation(fig, update_graph, interval=1000)  # noqa: F841

    try:
        print(
            f"{Fore.YELLOW}Starting Graph Window... (Close window to exit){Style.RESET_ALL}"
        )
        plt.show()
    except KeyboardInterrupt:
        print(f"\n{Fore.YELLOW}Exiting...{Style.RESET_ALL}")
        plt.close("all")  # Force close any lingering plot windows


if __name__ == "__main__":
    main()
