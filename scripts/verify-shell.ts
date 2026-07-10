import { spawn } from "node:child_process";
import { appendFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

type AutomationEvent = {
	type: string;
	time: string;
	[key: string]: unknown;
};

type VerifyResult = {
	ok: boolean;
	reason: string;
	reportPath: string;
	logPath: string;
	screenshotPath?: string;
	events: AutomationEvent[];
	exitCode: number | null;
};

const root = new URL("..", import.meta.url).pathname;
const outputDir = join(root, "artifacts", "automation");
const stamp = new Date().toISOString().replaceAll(":", "-");
const reportPath = join(outputDir, `shell-${stamp}.jsonl`);
const logPath = join(outputDir, `shell-${stamp}.log`);
const screenshotPath = join(outputDir, `shell-${stamp}.png`);
const timeoutMs = Number(process.env.TOI_VERIFY_TIMEOUT_MS ?? 20_000);

mkdirSync(outputDir, { recursive: true });
writeFileSync(logPath, "");

const child = spawn("bun", ["run", "dev"], {
	cwd: root,
	detached: true,
	env: {
		...process.env,
		TOI_AUTOMATION: "1",
		TOI_AUTOMATION_REPORT: reportPath,
	},
	stdio: ["ignore", "pipe", "pipe"],
});

let exitCode: number | null = null;
child.stdout.on("data", (chunk) => appendLog(chunk));
child.stderr.on("data", (chunk) => appendLog(chunk));
child.on("exit", (code) => {
	exitCode = code;
});

const started = Date.now();
let result: VerifyResult | undefined;
while (Date.now() - started < timeoutMs) {
	const events = readEvents(reportPath);
	const ready = events.find((event) => event.type === "viewport-ready") as AutomationEvent | undefined;
	const attached = events.find((event) => event.type === "viewport-attached") as AutomationEvent | undefined;
	if (ready?.ok === true && attached?.ok === true) {
		const rect = ready.rect as { width?: number; height?: number } | undefined;
		const expectedWidth = Math.max(1, Math.trunc(rect?.width ?? 0));
		const expectedHeight = Math.max(1, Math.trunc(rect?.height ?? 0));
		if (attached.width !== expectedWidth || attached.height !== expectedHeight) {
			result = {
				ok: false,
				reason: `native viewport resolution mismatch: DOM ${expectedWidth}x${expectedHeight}, native ${attached.width}x${attached.height}`,
				reportPath,
				logPath,
				screenshotPath: captureScreenshot(screenshotPath),
				events,
				exitCode,
			};
			break;
		}
		result = {
			ok: true,
			reason: `native viewport attached at ${expectedWidth}x${expectedHeight}`,
			reportPath,
			logPath,
			screenshotPath: captureScreenshot(screenshotPath),
			events,
			exitCode,
		};
		break;
	}
	const fatal = firstFatalLogLine(logPath);
	if (fatal !== undefined) {
		result = {
			ok: false,
			reason: fatal,
			reportPath,
			logPath,
			screenshotPath: captureScreenshot(screenshotPath),
			events,
			exitCode,
		};
		break;
	}
	if (exitCode !== null) {
		result = {
			ok: false,
			reason: `process exited before viewport ready (${exitCode})`,
			reportPath,
			logPath,
			screenshotPath: captureScreenshot(screenshotPath),
			events,
			exitCode,
		};
		break;
	}
	await sleep(250);
}

if (result === undefined) {
	const events = readEvents(reportPath);
	result = {
		ok: false,
		reason: `timeout after ${timeoutMs}ms`,
		reportPath,
		logPath,
		screenshotPath: captureScreenshot(screenshotPath),
		events,
		exitCode,
	};
}

stopProcessGroup(child.pid);
stopProjectProcesses(root);
console.log(JSON.stringify(result, null, 2));
process.exit(result.ok ? 0 : 1);

function appendLog(chunk: Buffer) {
	appendFileSync(logPath, chunk);
	process.stdout.write(chunk);
}

function readEvents(path: string): AutomationEvent[] {
	if (!existsSync(path)) {
		return [];
	}
	return readFileSync(path, "utf8")
		.split("\n")
		.filter(Boolean)
		.flatMap((line) => {
			try {
				return [JSON.parse(line) as AutomationEvent];
			} catch {
				return [];
			}
		});
}

function firstFatalLogLine(path: string): string | undefined {
	if (!existsSync(path)) {
		return undefined;
	}
	const fatalPatterns = [
		"pure virtual method called",
		"terminate called without an active exception",
		"Cannot create profile at path",
		"Signal 11",
		"SIGABRT",
		"Segmentation fault",
	];
	const lines = readFileSync(path, "utf8").split("\n");
	return lines.find((line) => fatalPatterns.some((pattern) => line.includes(pattern)));
}

function captureScreenshot(path: string): string | undefined {
	if (!process.env.DISPLAY) {
		return undefined;
	}
	try {
		Bun.spawnSync(["ffmpeg", "-y", "-f", "x11grab", "-i", process.env.DISPLAY, "-frames:v", "1", path], {
			stdout: "ignore",
			stderr: "ignore",
		});
		return existsSync(path) ? path : undefined;
	} catch {
		return undefined;
	}
}

function stopProcessGroup(pid: number | undefined) {
	if (pid === undefined) {
		return;
	}
	try {
		process.kill(-pid, "SIGTERM");
	} catch {}
	setTimeout(() => {
		try {
			process.kill(-pid, "SIGKILL");
		} catch {}
	}, 1_000).unref();
}

function stopProjectProcesses(projectRoot: string) {
	const pattern = join(projectRoot, "build", "dev-linux-x64", "TreesofInsanity-dev", "bin");
	for (const signal of ["TERM", "KILL"] as const) {
		Bun.spawnSync(["pkill", `-${signal}`, "-f", pattern], { stdout: "ignore", stderr: "ignore" });
	}
}

function sleep(ms: number) {
	return new Promise((resolve) => setTimeout(resolve, ms));
}
