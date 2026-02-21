{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  packages = with pkgs; [
    arduino-ide arduino-cli
    esptool
  ];
}
