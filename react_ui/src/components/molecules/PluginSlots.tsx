import { useState } from 'react';
import { cn } from '@/lib/utils';
import { useMixerStore } from '@/data/store';
import { ALL_INSTRUMENTS, ALL_CHANNEL_STRIPS } from '@/data/plugins';
import type { PluginSlot } from '@/data/slices/mixer';

interface PluginSlotButtonProps {
  slot: PluginSlot;
  label: string;
  icon: string;
  onSelect: (pluginId: string | null, pluginName: string | null) => void;
  onToggleBypass: () => void;
  onOpen: () => void;
  options: { id: string; name: string }[];
}

export function PluginSlotButton({ slot, label, icon, onSelect, onToggleBypass, onOpen, options }: PluginSlotButtonProps) {
  const [open, setOpen] = useState(false);

  const bypassDotClass = cn(
    bypassDot,
    slot.pluginId
      ? slot.bypassed ? bypassDotBypassed : bypassDotActive
      : bypassDotEmpty,
  );

  const nameClass = cn(
    nameBtn,
    slot.pluginId
      ? slot.bypassed ? nameBypassed : nameLoaded
      : nameEmpty,
  );

  return (
    <div className={slotWrapper}>
      <div className={slotRow}>
        <button onClick={onToggleBypass} className={bypassDotClass} title={slot.bypassed ? 'Enable' : 'Bypass'} />
        <span className={typeIcon}>{icon}</span>
        <button onClick={() => setOpen(!open)} className={nameClass} title={slot.pluginName || label}>
          {slot.pluginName || label}
        </button>
        {slot.pluginId && (
          <button
            onClick={onOpen}
            className={openPluginBtn}
            title="Open plugin window"
          >
            ⤢
          </button>
        )}
      </div>

      {open && (
        <div className={dropdown}>
          <button
            onClick={() => { onSelect(null, null); setOpen(false); }}
            className={dropdownItemNone}
          >
            — None —
          </button>
          {options.map((p) => (
            <button
              key={p.id}
              onClick={() => { onSelect(p.id, p.name); setOpen(false); }}
              className={cn(dropdownItem, slot.pluginId === p.id && dropdownItemSelected)}
            >
              {p.name}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

interface PluginSlotsProps {
  trackId: number;
  trackType: 'midi' | 'audio';
  instrument: PluginSlot;
  channelStrip: PluginSlot;
}

export function PluginSlots({ trackId, trackType, instrument, channelStrip }: PluginSlotsProps) {
  return (
    <div className={slotsContainer}>
      {trackType === 'midi' && (
        <PluginSlotButton
          slot={instrument}
          label="+ Instrument"
          icon="🎹"
          options={ALL_INSTRUMENTS}
          onSelect={(pluginId, pluginName) =>
            useMixerStore.getState().setInstrument(trackId, pluginId, pluginName)
          }
          onToggleBypass={() =>
            useMixerStore.getState().toggleInstrumentBypass(trackId)
          }
          onOpen={() =>
            useMixerStore.getState().openPlugin(trackId, 'instrument')
          }
        />
      )}
      <PluginSlotButton
        slot={channelStrip}
        label="+ Strip"
        icon="🎛️"
        options={ALL_CHANNEL_STRIPS}
        onSelect={(pluginId, pluginName) =>
          useMixerStore.getState().setChannelStrip(trackId, pluginId, pluginName)
        }
        onToggleBypass={() =>
          useMixerStore.getState().toggleChannelStripBypass(trackId)
        }
        onOpen={() =>
          useMixerStore.getState().openPlugin(trackId, 'channelStrip')
        }
      />
    </div>
  );
}

// --- Style constants ---

const slotsContainer = `w-full px-1.5 space-y-0.5 mb-1.5`;

const slotWrapper = `relative w-full`;
const slotRow = `flex items-center gap-0.5`;

const bypassDot = `w-1.5 h-1.5 rounded-full shrink-0 transition-colors`;
const bypassDotActive = `bg-[hsl(var(--plugin-active))]`;
const bypassDotBypassed = `bg-[hsl(var(--plugin-bypassed))]`;
const bypassDotEmpty = `bg-[hsl(var(--plugin-empty))]`;

const typeIcon = `text-[8px] leading-none shrink-0 select-none`;

const nameBtn = `flex-1 h-4 rounded text-[7px] font-medium truncate px-1 text-left transition-colors`;
const nameLoaded = `bg-[hsl(var(--card))] text-[hsl(var(--foreground))] hover:bg-[hsl(var(--muted))]`;
const nameBypassed = `bg-[hsl(var(--card))]/60 text-[hsl(var(--muted-foreground))] line-through`;
const nameEmpty = `bg-[hsl(var(--card))]/40 text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--card))]`;

const openPluginBtn = `
  w-4 h-4 rounded shrink-0 flex items-center justify-center
  text-[9px] leading-none
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  hover:bg-[hsl(var(--muted))] transition-colors`;

const dropdown = `
  absolute top-5 left-0 z-50 w-36 max-h-40
  overflow-y-auto bg-[hsl(var(--card))] border border-[hsl(var(--border))] rounded shadow-xl`;

const dropdownItemNone = `
  w-full text-left px-2 py-1 text-[9px]
  text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--muted))] hover:text-[hsl(var(--foreground))]`;

const dropdownItem = `
  w-full text-left px-2 py-1 text-[9px]
  text-[hsl(var(--foreground))] hover:bg-[hsl(var(--muted))] transition-colors`;
const dropdownItemSelected = `text-[hsl(var(--progress))] bg-[hsl(var(--muted))]/50`;
