#!/bin/bash
mkdir -p ./build/merged
find . \( -type d \( -name .git -o -name build -o -name __pycache__ -o -name .cache -o -name .pytest_cache \) -o -name .gitignore \) -prune -o -print | zip -qu9 build/merged/bovnar.zip -@
tail -n +1 CMake* src/dom/* src/io/* src/lexer/* src/utils/* src/validator/* src/writer/* src/*.c include/* tests/*.c tests/json/* > build/merged/bvnr_src.txt
tail -n +1 python/bovnar/*.py python/tests/*.py python/*.toml build/merged/bvnr_src.txt > build/merged/bvnr_py_src.txt
tail -n +1 examples/* build/merged/bvnr_py_src.txt > build/merged/bvnr_py_src_exmpl.txt
tail -n +1 doc/* build/merged/bvnr_py_src_exmpl.txt > build/merged/bvnr_py_src_exmpl_doc.txt
tail -n +1 web/*.html build/merged/bvnr_py_src_exmpl_doc.txt > build/merged/bvnr_py_src_exmpl_doc_web.txt
