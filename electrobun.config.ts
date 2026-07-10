import type { ElectrobunConfig } from "electrobun";

export default {
	app: {
		name: "Trees of Insanity",
		identifier: "app.treesofinsanity.desktop",
		version: "0.0.1",
	},
	build: {
		bun: {
			entrypoint: "src/bun/index.ts",
		},
		copy: {
			"dist/index.html": "views/mainview/index.html",
			"dist/assets": "views/mainview/assets",
			assets: "assets",
		},
		watchIgnore: ["dist/**"],
		linux: {
			bundleCEF: false,
			bundleWGPU: false,
			defaultRenderer: "native",
		},
	},
} satisfies ElectrobunConfig;
