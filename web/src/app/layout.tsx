import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Gaomon S620 CFW",
  description: "Flash and configure the custom firmware for the Gaomon S620 tablet from your browser.",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
