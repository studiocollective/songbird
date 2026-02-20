import React from 'react';

interface MarkdownRendererProps {
  content: string;
  className?: string;
}

/**
 * Lightweight markdown renderer — no external dependencies.
 * Handles: headings, bold, italic, inline code, fenced code blocks,
 * bullet/numbered lists, and paragraphs.
 */
export function MarkdownRenderer({ content, className = '' }: MarkdownRendererProps) {
  const blocks = parseBlocks(content);
  return (
    <div className={`${baseStyle} ${className}`}>
      {blocks.map((block, i) => (
        <React.Fragment key={i}>{renderBlock(block, i)}</React.Fragment>
      ))}
    </div>
  );
}

// --- Block-level parsing ---

type Block =
  | { type: 'heading'; level: number; text: string }
  | { type: 'code'; lang: string; text: string }
  | { type: 'list'; ordered: boolean; items: string[] }
  | { type: 'paragraph'; text: string };

function parseBlocks(md: string): Block[] {
  const lines = md.split('\n');
  const blocks: Block[] = [];
  let i = 0;

  while (i < lines.length) {
    const line = lines[i];

    // Fenced code block
    const fenceMatch = line.match(/^```(\w*)/);
    if (fenceMatch) {
      const lang = fenceMatch[1] || '';
      const codeLines: string[] = [];
      i++;
      while (i < lines.length && !lines[i].startsWith('```')) {
        codeLines.push(lines[i]);
        i++;
      }
      i++; // skip closing ```
      blocks.push({ type: 'code', lang, text: codeLines.join('\n') });
      continue;
    }

    // Heading
    const headingMatch = line.match(/^(#{1,3})\s+(.+)/);
    if (headingMatch) {
      blocks.push({ type: 'heading', level: headingMatch[1].length, text: headingMatch[2] });
      i++;
      continue;
    }

    // Unordered list
    if (/^\s*[-*]\s+/.test(line)) {
      const items: string[] = [];
      while (i < lines.length && /^\s*[-*]\s+/.test(lines[i])) {
        items.push(lines[i].replace(/^\s*[-*]\s+/, ''));
        i++;
      }
      blocks.push({ type: 'list', ordered: false, items });
      continue;
    }

    // Ordered list
    if (/^\s*\d+[.)]\s+/.test(line)) {
      const items: string[] = [];
      while (i < lines.length && /^\s*\d+[.)]\s+/.test(lines[i])) {
        items.push(lines[i].replace(/^\s*\d+[.)]\s+/, ''));
        i++;
      }
      blocks.push({ type: 'list', ordered: true, items });
      continue;
    }

    // Blank line — skip
    if (line.trim() === '') {
      i++;
      continue;
    }

    // Paragraph — collect consecutive non-blank, non-special lines
    const paraLines: string[] = [];
    while (
      i < lines.length &&
      lines[i].trim() !== '' &&
      !lines[i].match(/^```/) &&
      !lines[i].match(/^#{1,3}\s/) &&
      !lines[i].match(/^\s*[-*]\s+/) &&
      !lines[i].match(/^\s*\d+[.)]\s+/)
    ) {
      paraLines.push(lines[i]);
      i++;
    }
    blocks.push({ type: 'paragraph', text: paraLines.join('\n') });
  }

  return blocks;
}

// --- Block rendering ---

function renderBlock(block: Block, key: number): React.ReactNode {
  switch (block.type) {
    case 'heading': {
      const Tag = (`h${block.level}` as 'h1' | 'h2' | 'h3');
      const sizes = { 1: headingH1, 2: headingH2, 3: headingH3 };
      return <Tag className={sizes[block.level as 1|2|3] || headingH3}>{inlineRender(block.text)}</Tag>;
    }
    case 'code':
      return (
        <pre className={codeBlock} key={key}>
          <code>{block.text}</code>
        </pre>
      );
    case 'list': {
      const Tag = block.ordered ? 'ol' : 'ul';
      return (
        <Tag className={block.ordered ? orderedList : unorderedList} key={key}>
          {block.items.map((item, j) => (
            <li key={j}>{inlineRender(item)}</li>
          ))}
        </Tag>
      );
    }
    case 'paragraph':
      return <p className={paragraph} key={key}>{inlineRender(block.text)}</p>;
  }
}

// --- Inline rendering ---

function inlineRender(text: string): React.ReactNode[] {
  // Process inline markdown: bold, italic, inline code
  const tokens: React.ReactNode[] = [];
  // Pattern: `code`, **bold**, *italic*
  const regex = /(`[^`]+`|\*\*[^*]+\*\*|\*[^*]+\*)/g;
  let lastIndex = 0;
  let match: RegExpExecArray | null;

  while ((match = regex.exec(text)) !== null) {
    // Push text before match
    if (match.index > lastIndex) {
      tokens.push(text.slice(lastIndex, match.index));
    }

    const m = match[0];
    if (m.startsWith('`')) {
      tokens.push(<code key={match.index} className={inlineCode}>{m.slice(1, -1)}</code>);
    } else if (m.startsWith('**')) {
      tokens.push(<strong key={match.index}>{m.slice(2, -2)}</strong>);
    } else if (m.startsWith('*')) {
      tokens.push(<em key={match.index}>{m.slice(1, -1)}</em>);
    }

    lastIndex = match.index + m.length;
  }

  // Remaining text
  if (lastIndex < text.length) {
    tokens.push(text.slice(lastIndex));
  }

  return tokens.length > 0 ? tokens : [text];
}

// --- Styles ---

const baseStyle = `text-xs leading-relaxed font-sans space-y-1.5`;

const headingH1 = `text-sm font-bold mt-2 mb-1`;
const headingH2 = `text-xs font-bold mt-2 mb-1`;
const headingH3 = `text-xs font-semibold mt-1.5 mb-0.5`;

const paragraph = `whitespace-pre-wrap`;

const codeBlock = `
  bg-[hsl(var(--card))] border border-[hsl(var(--border))]
  rounded px-2 py-1.5 text-[11px] font-mono overflow-x-auto
  whitespace-pre`;

const inlineCode = `
  bg-[hsl(var(--card))] border border-[hsl(var(--border))]
  rounded px-1 py-0.5 text-[11px] font-mono`;

const unorderedList = `list-disc list-inside space-y-0.5 pl-1`;
const orderedList = `list-decimal list-inside space-y-0.5 pl-1`;
