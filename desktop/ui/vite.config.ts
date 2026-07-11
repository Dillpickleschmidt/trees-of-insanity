import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import tailwindcss from "@tailwindcss/vite";
import { defineConfig } from "vite";
import solid from "vite-plugin-solid";

const rootDir = dirname(fileURLToPath(import.meta.url));

export default defineConfig({
	base: "./",
	root: resolve(rootDir, "src"),
	resolve: {
		alias: {
			"~": resolve(rootDir, "src"),
		},
	},
	plugins: [solid(), tailwindcss()],
	build: {
		outDir: resolve(rootDir, "../dist"),
		emptyOutDir: true,
	},
});
