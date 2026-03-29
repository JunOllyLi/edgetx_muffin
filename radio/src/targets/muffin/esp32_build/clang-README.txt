Current clang in python3 supports clang-17

sudo apt install clang-17
cd /usr/lib/llvm-17/lib
sudo ln -s libclang.so.1 libclang.so
sudo ln -s libclang-17.so.1 libclang-17.so
