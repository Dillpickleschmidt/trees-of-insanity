export type UiTheme = "system" | "light" | "dark";
export type ColorTheme = "neutral" | "catppuccin" | "nord" | "tokyo-night";
export type PlantTypePresetKey =
	| "a"
	| "b"
	| "c"
	| "d"
	| "e"
	| "f"
	| "g"
	| "h"
	| "i"
	| "j"
	| "k"
	| "l"
	| "m"
	| "n"
	| "o"
	| "p";
export type StatusTone = "live" | "idle" | "error";

export const uiThemes: UiTheme[] = ["system", "light", "dark"];
export const colorThemes: ColorTheme[] = ["neutral", "catppuccin", "nord", "tokyo-night"];
export const plantTypePresetKeys: PlantTypePresetKey[] = [
	"a",
	"b",
	"c",
	"d",
	"e",
	"f",
	"g",
	"h",
	"i",
	"j",
	"k",
	"l",
	"m",
	"n",
	"o",
	"p",
];

export const uiThemeLabels: Record<UiTheme, string> = {
	system: "System",
	light: "Light",
	dark: "Dark",
};

export const colorThemeLabels: Record<ColorTheme, string> = {
	neutral: "Neutral",
	catppuccin: "Catppuccin",
	nord: "Nord",
	"tokyo-night": "Tokyo Night",
};

export function plantTypePresetLabel(key: PlantTypePresetKey) {
	return `Plant Type ${key.toUpperCase()}`;
}

export function storedTheme<T extends string>(key: string, fallback: T, allowed: readonly T[]) {
	const value = window.localStorage.getItem(key) as T | null;
	return value && allowed.includes(value) ? value : fallback;
}

export function storedNumber(key: string, fallback: number, minimum: number, maximum: number) {
	const value = Number(window.localStorage.getItem(key));
	return Number.isFinite(value) && value >= minimum && value <= maximum ? value : fallback;
}
