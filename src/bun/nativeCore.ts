// Bun FFI binding to the native core shared library (libtoi_native_core).
//
// The native core owns all Project/growth/render state. This module is a thin,
// typed wrapper over its C ABI (see native/include/toi/native/native_core.h):
//
//   ToiNativeCore* toi_create(const char* options_json);
//   void           toi_destroy(ToiNativeCore*);
//   char*          toi_handle_command(ToiNativeCore*, const char* request_json);
//   char*          toi_last_error_json();
//   void           toi_free_string(char*);
//
// Memory ownership: every char* returned by the core is heap-allocated by the
// core and must be released with toi_free_string. We copy it into a JS string
// immediately and free it, so no C memory outlives a single call.

import { CString, dlopen, FFIType, type Pointer, suffix } from "bun:ffi";
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import type { CommandMethod, CommandParams, CommandRequest, CommandResponse, CommandResult } from "../shared/appCommands";
import type { ViewportCameraInput, ViewportStatus } from "../shared/shellRpc";

export type NativeCoreOptions = {
	projectPath?: string;
	assetRootPath?: string;
	prototypeAssetPath?: string;
};

export type ViewportAttachResult = {
	ok: boolean;
	error?: string;
	device?: string;
	width?: number;
	height?: number;
};

export type ViewportResizeResult = {
	ok: boolean;
	error?: string;
};

export class NativeCoreCommandError extends Error {
	readonly code: string | undefined;

	constructor(message: string, code: string | undefined) {
		super(message);
		this.name = "NativeCoreCommandError";
		this.code = code;
	}
}

export class NativeCore {
	#handle: Pointer;
	#closed = false;

	private constructor(handle: Pointer) {
		this.#handle = handle;
	}

	static open(options: NativeCoreOptions = {}): NativeCore {
		const payload: Record<string, string> = {};
		if (options.projectPath) payload.project_path = options.projectPath;
		if (options.assetRootPath) payload.asset_root_path = options.assetRootPath;
		if (options.prototypeAssetPath) payload.prototype_asset_path = options.prototypeAssetPath;

		const handle = lib().symbols.toi_create(cString(JSON.stringify(payload)));
		if (handle === null) {
			throw new Error(`toi_create failed: ${lastErrorJson()}`);
		}
		return new NativeCore(handle);
	}

	// Raw pass-through: forwards a full command request and returns the full
	// response object. Used by the RPC bridge, which relays UI requests as-is.
	request(req: CommandRequest): CommandResponse {
		if (this.#closed) {
			throw new Error("native core is closed");
		}
		const requestBuf = cString(JSON.stringify(req));
		const resultPtr = lib().symbols.toi_handle_command(this.#handle, requestBuf);
		return JSON.parse(readCoreString(resultPtr)) as CommandResponse;
	}

	// Typed convenience: issues one command and returns its result, throwing a
	// NativeCoreCommandError when the core reports ok:false.
	command<M extends CommandMethod>(method: M, params?: CommandParams<M>): CommandResult<M> {
		const response = this.request({ method, params } as CommandRequest) as CommandResponse<M>;
		if (!response.ok) {
			throw new NativeCoreCommandError(response.error, response.code);
		}
		return response.result;
	}

	// Attach the ovrtx/Vulkan viewport to Electrobun's native child window.
	attachViewport(nativeWindow: Pointer, width: number, height: number): ViewportAttachResult {
		if (this.#closed) {
			throw new Error("native core is closed");
		}
		const resultPtr = lib().symbols.toi_attach_viewport(
			this.#handle,
			nativeWindow,
			Math.trunc(width),
			Math.trunc(height),
		);
		return JSON.parse(readCoreString(resultPtr)) as ViewportAttachResult;
	}

	resizeViewport(width: number, height: number): ViewportResizeResult {
		if (this.#closed) {
			throw new Error("native core is closed");
		}
		const resultPtr = lib().symbols.toi_resize_viewport(this.#handle, Math.trunc(width), Math.trunc(height));
		return JSON.parse(readCoreString(resultPtr)) as ViewportResizeResult;
	}

	viewportSurfaceChanged(): void {
		if (!this.#closed) {
			lib().symbols.toi_viewport_surface_changed(this.#handle);
		}
	}

	viewportCameraInput(input: ViewportCameraInput): void {
		if (this.#closed) {
			return;
		}
		const kinds = { orbit: 0, pan: 1, dolly: 2, wheel: 3 } as const;
		lib().symbols.toi_viewport_camera_input(
			this.#handle,
			kinds[input.kind],
			input.dx,
			input.dy,
			Math.max(1, Math.trunc(input.viewportHeight)),
		);
	}

	viewportStatus(): ViewportStatus {
		if (this.#closed) {
			throw new Error("native core is closed");
		}
		const resultPtr = lib().symbols.toi_viewport_status(this.#handle);
		const result = JSON.parse(readCoreString(resultPtr)) as ViewportStatus | { error: string };
		if ("error" in result) {
			throw new Error(result.error);
		}
		return result;
	}

	detachViewport(): void {
		if (!this.#closed) {
			lib().symbols.toi_detach_viewport(this.#handle);
		}
	}

	close(): void {
		if (this.#closed) {
			return;
		}
		lib().symbols.toi_destroy(this.#handle);
		this.#closed = true;
	}
}

type NativeLibrary = ReturnType<typeof openLibrary>;

let cachedLib: NativeLibrary | undefined;

function lib(): NativeLibrary {
	cachedLib ??= openLibrary();
	return cachedLib;
}

function openLibrary() {
	return dlopen(resolveNativeCorePath(), {
		toi_create: { args: [FFIType.ptr], returns: FFIType.ptr },
		toi_destroy: { args: [FFIType.ptr], returns: FFIType.void },
		toi_handle_command: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.ptr },
		toi_last_error_json: { args: [], returns: FFIType.ptr },
		toi_free_string: { args: [FFIType.ptr], returns: FFIType.void },
		toi_attach_viewport: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32, FFIType.i32], returns: FFIType.ptr },
		toi_resize_viewport: { args: [FFIType.ptr, FFIType.i32, FFIType.i32], returns: FFIType.ptr },
		toi_viewport_surface_changed: { args: [FFIType.ptr], returns: FFIType.void },
		toi_viewport_camera_input: {
			args: [FFIType.ptr, FFIType.i32, FFIType.f32, FFIType.f32, FFIType.i32],
			returns: FFIType.void,
		},
		toi_viewport_status: { args: [FFIType.ptr], returns: FFIType.ptr },
		toi_detach_viewport: { args: [FFIType.ptr], returns: FFIType.void },
	});
}

// Reads a heap-allocated C string returned by the core and releases it.
function readCoreString(resultPtr: Pointer | null): string {
	if (resultPtr === null) {
		throw new Error("native core returned a null string");
	}
	const text = new CString(resultPtr).toString();
	lib().symbols.toi_free_string(resultPtr);
	return text;
}

function lastErrorJson(): string {
	const ptr = lib().symbols.toi_last_error_json();
	return ptr === null ? '{"ok":false,"error":"no error information"}' : readCoreString(ptr);
}

// NUL-terminated UTF-8 buffer. Bun pins the TypedArray for the duration of the
// FFI call and passes a pointer to its backing store for FFIType.ptr args.
function cString(value: string): Uint8Array {
	return new TextEncoder().encode(`${value}\0`);
}

// Keep project data under Electrobun user data; resolve read-only assets from
// Electrobun resources or the checkout.
export function defaultNativeCoreOptions(userDataPath: string): NativeCoreOptions {
	const root = findRepoRoot(process.cwd()) ?? findRepoRoot(import.meta.dir);
	const packagedAssets = join(dirname(import.meta.dir), "assets");
	const assetRoot = existsSync(packagedAssets) ? packagedAssets : join(root ?? process.cwd(), "assets");
	return {
		projectPath: join(userDataPath, "toi.project.json"),
		assetRootPath: assetRoot,
		prototypeAssetPath: join(assetRoot, "prototypes", "TOI_Module_Prototypes.obj"),
	};
}

export function resolveNativeCorePath(): string {
	const fileName = `libtoi_native_core.${suffix}`;
	const relative = [
		join("build", "ovrtx", fileName), // desktop app
		join("build", "core", fileName), // FFI tests
	];

	const roots = candidateRoots();
	for (const root of roots) {
		for (const rel of relative) {
			const candidate = join(root, rel);
			if (existsSync(candidate)) {
				return candidate;
			}
		}
	}

	throw new Error(`could not locate ${fileName}; searched ${roots.join(", ")}. Build the native core first.`);
}

function candidateRoots(): string[] {
	const starts = [process.cwd(), import.meta.dir, dirname(import.meta.dir)];
	const roots = new Set<string>();
	for (const start of starts) {
		roots.add(start);
		const repoRoot = findRepoRoot(start);
		if (repoRoot) {
			roots.add(repoRoot);
		}
	}
	return [...roots];
}

function findRepoRoot(start: string): string | undefined {
	let current = start;
	for (;;) {
		if (existsSync(join(current, "CMakeLists.txt"))) {
			return current;
		}
		const parent = dirname(current);
		if (parent === current) {
			return undefined;
		}
		current = parent;
	}
}
