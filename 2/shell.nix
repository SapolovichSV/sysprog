{ pkgs ? import <nixpkgs> { } }:

let
  # Определяем пользовательские пути здесь, в секции let
  customIncludePaths = [
    "../utils"           # Относительный путь к unit.h
    "/abs/path/to/libs"  # Пример абсолютного пути
    "./include"
    "$PWD/utils_link"          # Локальная папка в проекте
  ];
in
pkgs.mkShell {
  # Необходимые инструменты
  nativeBuildInputs = [
    pkgs.clang-tools
    pkgs.bear
    pkgs.gnumake
    pkgs.cmake
    pkgs.python314
  ];

  # Заголовочные файлы
  buildInputs = [
    pkgs.glibc.dev
    pkgs.libcxx
  ];

  # Настройка переменных окружения
  shellHook = ''
    ln -sfn $(realpath ../utils) $PWD/utils_link
    # Пути к стандартным заголовкам + пользовательские пути
    export C_INCLUDE_PATH="\
      ${pkgs.glibc.dev}/include:\
      ${pkgs.libcxx}/include/c++/v1:\
      ${builtins.concatStringsSep ":" customIncludePaths}"
    
    # Для компилятора GCC
    export CPLUS_INCLUDE_PATH="\
      ${pkgs.gcc-unwrapped.lib}/gcc/*/*/include:\
      $C_INCLUDE_PATH"

    echo "=== Окружение настроено ==="
    echo "C_INCLUDE_PATH: $C_INCLUDE_PATH"
    echo "CPLUS_INCLUDE_PATH: $CPLUS_INCLUDE_PATH"
  '';
}