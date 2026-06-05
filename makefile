.PHONY: prepare build release clean clangd

prepare:
	pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Action prepare

build:
	pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Action build

release:
	pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Action release

clean:
	pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Action clean

clangd:
	pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Action clangd
