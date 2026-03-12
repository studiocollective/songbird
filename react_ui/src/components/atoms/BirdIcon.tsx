interface BirdIconProps {
  size?: number;
  className?: string;
}

/**
 * Inline SVG songbird icon — replaces the 🐦 emoji throughout the UI.
 * Inherits currentColor so it adapts to any text/theme colour.
 */
export function BirdIcon({ size = 16, className }: BirdIconProps) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.8"
      strokeLinecap="round"
      strokeLinejoin="round"
      className={className}
      aria-hidden="true"
    >
      {/* Body */}
      <path d="M16 7c0-2.21-1.79-4-4-4S8 4.79 8 7c0 .74.2 1.44.56 2.03" />
      {/* Head / top curve */}
      <path d="M12 3c1.1 0 2.12.37 2.93 1" />
      {/* Wing */}
      <path d="M9.5 9.5C7.5 11 5 12.5 3 13c2.5.5 5 .2 7-1" />
      {/* Tail */}
      <path d="M16 7c1.5 1 3 3 3 6 0 2-1 3.5-2 4.5" />
      {/* Belly */}
      <path d="M8.56 9.03C7.6 10.2 7 11.7 7 13.5c0 3.04 2.46 5.5 5.5 5.5 2.16 0 4.03-1.25 4.93-3.06" />
      {/* Beak */}
      <path d="M14 5l2-1.5L14 4.5" />
      {/* Eye */}
      <circle cx="11" cy="6" r="0.5" fill="currentColor" stroke="none" />
      {/* Legs */}
      <path d="M10 19v2.5M10 21.5l-1 .5M10 21.5l1 .5" />
      <path d="M14 19v2M14 21l-1 .5M14 21l1 .5" />
    </svg>
  );
}
