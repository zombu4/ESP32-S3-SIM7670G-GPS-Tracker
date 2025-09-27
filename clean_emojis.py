#!/usr/bin/env python3
"""
Systematically remove ALL emojis from C/H source files
"""
import os
import re
import shutil

def clean_emojis_from_file(file_path):
    """Remove all emojis from a file while preserving formatting"""
    try:
        # Backup first
        backup_path = file_path + ".bak"
        shutil.copy2(file_path, backup_path)
        
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
        
        # Common emojis used in the project - comprehensive list
        emoji_replacements = {
            '📡': '',
            '🔍': '',
            '🎉': '',
            '🛰️': '',
            '⏰': '',
            '📍': '',
            '🌐': '',
            '💬': '',
            '✅': '',
            '❌': '',
            '🔋': '',
            '📊': '',
            '📄': '',
            '🚧': '',
            '🎯': '',
            '🔧': '',
            '📚': '',
            '⚡': '',
            '🟡': '',
            '🔄': '',
            '📱': '',
            '🆔': '',
            '🔒': '',
            '🚨': '',
            '📈': '',
            '📉': '',
            '💾': '',
            '🔌': '',
            '⚙️': '',
            '🛡️': '',
            '📋': '',
            '🎨': '',
            '🔓': '',
            '🔔': '',
            '🎭': '',
            '🎪': '',
            '🚀': '',
            '💡': '',
            '🔥': '',
            '⭐': '',
            '🎊': '',
            '🎈': '',
            '🎁': '',
            '🆕': '',
            '🔝': '',
            '💯': '',
            '⚠️': '',
            '🏓': '',
            '🧪': '',
            '📭': '',
            '📶': '',
            '🔗': '',
            '⏳': ''
        }
        
        original_content = content
        
        # Replace each emoji
        for emoji, replacement in emoji_replacements.items():
            content = content.replace(emoji, replacement)
        
        # Remove any extra spaces that might be left
        lines = content.split('\n')
        cleaned_lines = []
        for line in lines:
            # Remove double spaces after emoji removal
            cleaned_line = re.sub(r'  +', ' ', line)
            # Remove trailing spaces from log messages
            cleaned_line = re.sub(r'ESP_LOG[A-Z]\(TAG, "[^"]*\s+"', lambda m: m.group(0).rstrip(' "') + '"', cleaned_line)
            cleaned_lines.append(cleaned_line)
        
        content = '\n'.join(cleaned_lines)
        
        if content != original_content:
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(content)
            return True
        return False
        
    except Exception as e:
        print(f"Error processing {file_path}: {e}")
        return False

def main():
    """Clean all emojis from C/H files"""
    files_cleaned = 0
    
    # Process all C/H files in main directory
    for root, dirs, files in os.walk('main'):
        for file in files:
            if file.endswith(('.c', '.h')):
                file_path = os.path.join(root, file)
                if clean_emojis_from_file(file_path):
                    print(f"Cleaned emojis from: {file_path}")
                    files_cleaned += 1
    
    # Also clean .gitignore and copilot-instructions.md
    extra_files = ['.gitignore', '.github/copilot-instructions.md']
    for file_path in extra_files:
        if os.path.exists(file_path):
            if clean_emojis_from_file(file_path):
                print(f"Cleaned emojis from: {file_path}")
                files_cleaned += 1
    
    print(f"\nCompleted! Cleaned {files_cleaned} files of all emojis.")
    print("Backup files created with .bak extension")

if __name__ == "__main__":
    main()