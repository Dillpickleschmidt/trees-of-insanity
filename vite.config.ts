import { defineConfig } from "vite";
import solid from "vite-plugin-solid";

export default defineConfig({
	base: "./",
	plugins: [solid()],
	root: "src/mainview",
	build: {
		outDir: "../../dist",
		emptyOutDir: true,
	},
});
