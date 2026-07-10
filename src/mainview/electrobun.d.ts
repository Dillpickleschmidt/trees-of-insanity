import type { JSX as SolidJSX } from "solid-js";

declare global {
	type WgpuTagElement = HTMLElement & {
		wgpuViewId: number | null;
		transparent: boolean;
		passthroughEnabled: boolean;
		hidden: boolean;
		toggleTransparent(transparent?: boolean): void;
		togglePassthrough(enablePassthrough?: boolean): void;
		toggleHidden(hidden?: boolean): void;
		syncDimensions(force?: boolean): void;
		runTest(): void;
		addMaskSelector(selector: string): void;
		removeMaskSelector(selector: string): void;
		on(event: "ready", listener: (event: CustomEvent<{ id: number }>) => void): void;
		off(event: "ready", listener: (event: CustomEvent<{ id: number }>) => void): void;
	};
}

declare module "solid-js" {
	namespace JSX {
		interface IntrinsicElements {
			"electrobun-wgpu": SolidJSX.HTMLAttributes<WgpuTagElement> & {
				id?: string;
				class?: string;
				passthrough?: boolean;
			};
		}
	}
}

export {};
