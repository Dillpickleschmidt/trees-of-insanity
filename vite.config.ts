import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import tailwindcss from "@tailwindcss/vite";
import { defineConfig } from "vite";
import solid from "vite-plugin-solid";

const rootDir = dirname(fileURLToPath(import.meta.url));

export default defineConfig({
	base: "./",
	root: "src/mainview",
	resolve: {
		alias: {
			"~": resolve(rootDir, "src/mainview"),
		},
	},
	plugins: [solid(), tailwindcss()],
	build: {
		outDir: "../../dist",
		emptyOutDir: true,
	},
});
