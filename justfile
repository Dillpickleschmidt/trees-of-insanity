dev:
	bun run dev

check:
	bun run typecheck
	bun run build:ui
	cmake --preset core
	cmake --build --preset core
	ctest --preset core

core-config:
	cmake --preset core

core-build:
	cmake --build --preset core

core-test:
	ctest --preset core

desktop-build:
	bun run build
