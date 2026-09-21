import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";

export const dynamic = "force-dynamic";

const FILE = resolve(process.env.FLASH_COUNT_FILE ?? "data/flash-count.json");
const MIN_GAP_MS = 30_000;   // one count per client per 30 s

let queue: Promise<unknown> = Promise.resolve();
const lastSeen = new Map<string, number>();

async function readCount(): Promise<number> {
  try {
    const n = JSON.parse(await readFile(FILE, "utf8")).count;
    return Number.isInteger(n) && n >= 0 ? n : 0;
  } catch {
    return 0;
  }
}

// Requests are handled one after another so two flashes finishing together are both counted.
function increment(): Promise<number> {
  const next = queue.then(async () => {
    const count = (await readCount()) + 1;
    await mkdir(dirname(FILE), { recursive: true });
    await writeFile(`${FILE}.tmp`, JSON.stringify({ count }));
    await rename(`${FILE}.tmp`, FILE);
    return count;
  });
  queue = next.catch(() => {});
  return next;
}

export async function GET() {
  return Response.json({ count: await readCount() });
}

export async function POST(req: Request) {
  const client = req.headers.get("x-forwarded-for")?.split(",")[0].trim() ?? "local";
  const now = Date.now();
  if (now - (lastSeen.get(client) ?? 0) < MIN_GAP_MS) {
    return Response.json({ count: await readCount(), counted: false });
  }
  lastSeen.set(client, now);
  if (lastSeen.size > 1000) for (const [k, t] of lastSeen) if (now - t > MIN_GAP_MS) lastSeen.delete(k);
  return Response.json({ count: await increment(), counted: true });
}
