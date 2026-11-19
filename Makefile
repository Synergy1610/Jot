.PHONY: dev typecheck build

dev:
	pnpm tauri dev

typecheck:
	pnpm typecheck

build:
	pnpm tauri build
