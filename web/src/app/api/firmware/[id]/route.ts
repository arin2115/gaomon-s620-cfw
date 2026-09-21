import { load } from "../../../../server/firmware";

export const dynamic = "force-dynamic";

export async function GET(_req: Request, ctx: { params: Promise<{ id: string }> }) {
  const { id } = await ctx.params;
  const fw = await load(id);
  if (!fw) return new Response("Not found", { status: 404 });
  return new Response(new Uint8Array(fw.data), {
    headers: {
      "Content-Type": "application/octet-stream",
      "Content-Disposition": `attachment; filename="${id === "original" ? "s620_original.bin" : "s620.bin"}"`,
      "Cache-Control": "no-store",
      "X-Firmware-Sha256": fw.info.sha256,
    },
  });
}
