// Typed UI client for actions dispatched through the Qt WebChannel adapter.

import type {
	CommandMethod,
	CommandParamsArg,
	CommandRequest,
	CommandResponse,
	CommandResult,
} from "./shared/desktopActions";

export class AppCommandError extends Error {
	readonly code: string | undefined;

	constructor(message: string, code: string | undefined) {
		super(message);
		this.name = "AppCommandError";
		this.code = code;
	}
}

export type AppCommandTransport = (request: CommandRequest) => Promise<CommandResponse>;

export type AppClient = {
	command<M extends CommandMethod>(method: M, ...params: CommandParamsArg<M>): Promise<CommandResult<M>>;
};

export function createAppClient(send: AppCommandTransport): AppClient {
	let nextId = 0;
	return {
		async command(method, ...params) {
			const request = { id: nextId++, method, params: params[0] } as CommandRequest;
			const response = (await send(request)) as CommandResponse<typeof method>;
			if (!response.ok) {
				throw new AppCommandError(response.error, response.code);
			}
			return response.result;
		},
	};
}
