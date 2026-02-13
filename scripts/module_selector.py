#!/usr/bin/env python3
"""
Interactive Module Selector for PlatformIO Compilation

This tool allows users to interactively select optional functionalities
for compilation, tracking memory usage and generating the appropriate
PlatformIO command with selected options.
"""

import json
import os
import sys
from typing import Dict, List, Any

try:
    import colorama
    from colorama import Fore, Back, Style
    colorama.init()
    HAS_COLORAMA = True
except ImportError:
    HAS_COLORAMA = False
    # Define dummy colors if colorama not available
    class DummyColor:
        def __getattr__(self, name):
            return ''
    Fore = Back = Style = DummyColor()

class ModuleSelector:
    def __init__(self, config_file: str):
        self.config_file = config_file
        self.config = self.load_config()
        self.selected = [False] * len(self.config['functionalities'])
        self.current_memory = self.config['base_size']

    def load_config(self) -> Dict[str, Any]:
        """Load configuration from JSON file."""
        try:
            with open(self.config_file, 'r') as f:
                return json.load(f)
        except FileNotFoundError:
            print(f"{Fore.RED}Error: Configuration file '{self.config_file}' not found.{Style.RESET_ALL}")
            sys.exit(1)
        except json.JSONDecodeError as e:
            print(f"{Fore.RED}Error: Invalid JSON in configuration file: {e}{Style.RESET_ALL}")
            sys.exit(1)

    def calculate_memory_usage(self) -> int:
        """Calculate current memory usage based on selected options."""
        memory = self.config['base_size']
        for i, selected in enumerate(self.selected):
            if selected:
                memory += self.config['functionalities'][i]['size']
        return memory

    def display_header(self):
        """Display the tool header and current memory status."""
        os.system('cls' if os.name == 'nt' else 'clear')
        print(f"{Fore.CYAN}{Style.BRIGHT}{self.config['tool_name']}{Style.RESET_ALL}")
        print("=" * 50)

        current_memory = self.calculate_memory_usage()
        max_memory = self.config['max_memory']

        memory_color = Fore.GREEN
        if current_memory > max_memory * 0.9:
            memory_color = Fore.RED
        elif current_memory > max_memory * 0.8:
            memory_color = Fore.YELLOW

        print(f"Base size: {self.config['base_size']} KB")
        print(f"Current usage: {memory_color}{current_memory} KB{Style.RESET_ALL}")
        print(f"Maximum: {max_memory} KB")

        if current_memory > max_memory:
            print(f"{Fore.RED}{Style.BRIGHT}WARNING: Memory limit exceeded!{Style.RESET_ALL}")
        print()

    def display_options(self):
        """Display the list of functionalities with selection status."""
        print("Available functionalities:")
        print("-" * 30)

        for i, func in enumerate(self.config['functionalities']):
            status = f"{Fore.GREEN}[X]{Style.RESET_ALL}" if self.selected[i] else "[ ]"
            size_color = Fore.YELLOW if self.selected[i] else Fore.WHITE
            print(f"{i+1:2d}. {status} {func['description']} ({size_color}+{func['size']} KB{Style.RESET_ALL})")

        print()

    def display_menu(self):
        """Display the menu options."""
        print("Commands:")
        print("  <number>  - Toggle selection for that functionality")
        print("  a         - Select all")
        print("  n         - Select none")
        print("  w         - Write command and exit")
        print("  q         - Quit without saving")
        print()

    def toggle_selection(self, index: int):
        """Toggle the selection status of a functionality."""
        if 0 <= index < len(self.selected):
            self.selected[index] = not self.selected[index]

    def select_all(self):
        """Select all functionalities."""
        self.selected = [True] * len(self.selected)

    def select_none(self):
        """Deselect all functionalities."""
        self.selected = [False] * len(self.selected)

    def generate_command(self) -> str:
        """Generate the PlatformIO command with selected options."""
        cmd = self.config['pio_command']
        params = []

        for i, selected in enumerate(self.selected):
            if selected:
                params.extend(self.config['functionalities'][i]['params'])

        if params:
            cmd += " " + " ".join(params)

        return cmd

    def write_command(self):
        """Write the generated command to stdout."""
        command = self.generate_command()
        print(f"\n{Fore.GREEN}Generated command:{Style.RESET_ALL}")
        print(command)
        print(f"\n{Fore.CYAN}Copy and paste this command to compile with selected options.{Style.RESET_ALL}")

    def run(self):
        """Main interactive loop."""
        while True:
            self.display_header()
            self.display_options()
            self.display_menu()

            try:
                choice = input("Enter your choice: ").strip().lower()

                if choice == 'q':
                    print("Exiting without changes.")
                    break
                elif choice == 'w':
                    self.write_command()
                    break
                elif choice == 'a':
                    self.select_all()
                elif choice == 'n':
                    self.select_none()
                elif choice.isdigit():
                    index = int(choice) - 1
                    self.toggle_selection(index)
                else:
                    print(f"{Fore.YELLOW}Invalid choice. Please try again.{Style.RESET_ALL}")
                    input("Press Enter to continue...")

            except KeyboardInterrupt:
                print("\nInterrupted. Exiting.")
                break
            except EOFError:
                print("\nEOF received. Exiting.")
                break

def main():
    if len(sys.argv) != 2:
        print("Usage: python module_selector.py <config_file>")
        sys.exit(1)

    config_file = sys.argv[1]
    selector = ModuleSelector(config_file)
    selector.run()

if __name__ == "__main__":
    main()