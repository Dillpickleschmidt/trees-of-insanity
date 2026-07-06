// FFI smoke test: drives the real libtoi_native_core.so through the Bun binding.
// Requires the native core to be built first (`cmake --build --preset core`).

import { afterAll, expect, test } from "bun:test";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { NativeCore, NativeCoreCommandError } from "../src/bun/nativeCore";

const root = process.cwd();
const projectPath = join(mkdtempSync(join(tmpdir(), "toi-ffi-")), "toi.project.json");

const core = NativeCore.open({
	projectPath,
	assetRootPath: join(root, "assets"),
	prototypeAssetPath: join(root, "assets", "prototypes", "TOI_Module_Prototypes.obj"),
});

afterAll(() => core.close());

test("app.get_state opens the default module workspace", () => {
	const state = core.command("app.get_state");
	expect(state.active_workspace).toBe("module");
	expect(state.active_prototype_id).toBe(8);
	expect(state.active_plant_type_id).toBe("plant-type-1");
	expect(state.prototypes.length).toBe(9);
	expect(state.plant_type_parameter_descriptors.length).toBe(13);
});

test("age scrub round-trips through the growth summary", () => {
	core.command("module.set_age", { age: 0 });
	const summary = core.command("module.get_growth_snapshot_summary");
	expect(summary.module_physiological_age).toBe(0);
	expect(summary.visible_segment_count).toBeGreaterThanOrEqual(0);
});

test("plant type create/update/delete mutates the library", () => {
	const created = core.command("plant_types.create", { name: "Test Fir", preset_key: "c" });
	expect(created.name).toBe("Test Fir");

	core.command("plant_types.update", { plant_type_id: created.id, name: "Renamed Fir" });
	const list = core.command("plant_types.list");
	expect(list.find((entry) => entry.id === created.id)?.name).toBe("Renamed Fir");

	core.command("plant_types.delete", { plant_type_id: created.id });
	const after = core.command("plant_types.list");
	expect(after.some((entry) => entry.id === created.id)).toBe(false);
});

test("unknown method surfaces a command error", () => {
	// Raw request keeps the error observable instead of throwing.
	const response = core.request({ method: "does.not.exist" } as never);
	expect(response.ok).toBe(false);

	expect(() => core.command("plant_types.delete", { plant_type_id: "missing" })).toThrow(NativeCoreCommandError);
});
