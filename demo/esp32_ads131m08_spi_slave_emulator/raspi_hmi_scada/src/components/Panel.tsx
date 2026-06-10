import type { ReactNode } from "react";

interface Props {
  title?: string;
  children: ReactNode;
  className?: string;
  id?: string;
}

export function Panel({ title, children, className = "", id }: Props) {
  return (
    <section id={id} className={`industrial-panel rounded-sm p-3 ${className}`}>
      {title ? (
        <h2 className="mb-2 flex items-center gap-2 text-xs font-black uppercase tracking-[0.08em] text-[#AFC4D8]">
          <span className="h-3 w-1 bg-cyan" />
          {title}
        </h2>
      ) : null}
      {children}
    </section>
  );
}
