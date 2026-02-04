#!/usr/bin/env python3

import os
import re
import glob
from pathlib import Path

def update_cmake_lists():

    project_name = Path.cwd().name
    print(f"Project name: {project_name}")
    
    c_files = glob.glob("src/**/*.c", recursive=True)
    c_files.sort()
    print(f"Find the {len(c_files)} .c files")
    
    with open("CMakeLists.txt", "r", encoding="utf-8") as f:
        content = f.read()
    
    content = re.sub(
        r'project\s*\(\s*\w+\s*\)', 
        f'project({project_name})', 
        content
    )
    
    content = re.sub(
        r'target_sources\s*\(\s*app\s+PRIVATE[^)]*\)\s*\n?',
        '',
        content,
        flags=re.IGNORECASE | re.DOTALL
    )
    
    if not content.endswith('\n'):
        content += '\n'
    
    new_sources_section = "target_sources(app PRIVATE\n"
    for c_file in c_files:
        new_sources_section += f"    {c_file}\n"
    new_sources_section += ")\n"
    
    content += new_sources_section
    
    with open("CMakeLists.txt", "w", encoding="utf-8") as f:
        f.write(content)
    
    print("CMakeLists.txt update done")

if __name__ == "__main__":
    update_cmake_lists()