import { list } from "../../../server/firmware";

export const dynamic = "force-dynamic";

export async function GET() {
  return Response.json(await list(), { headers: { "Cache-Control": "no-store" } });
}
