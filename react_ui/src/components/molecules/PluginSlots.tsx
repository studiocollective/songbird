import { useState } from 'react';
import { cn } from '@/lib/utils';
import { useMixerStore } from '@/data/store';
import type { PluginSlot, Track } from '@/data/slices/mixer';

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
        <button
          onClick={onOpen}
          className={cn(openPluginBtn, !slot.pluginId && 'invisible')}
          title="Open plugin window"
        >
          ⤢
        </button>
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

interface SidechainSelectorProps {
  trackId: number;
  sidechainTrackId: number | null;
  sidechainSensitivity: number;
  trackList: Track[];
}

function SidechainSelector({ trackId, sidechainTrackId, sidechainSensitivity, trackList }: SidechainSelectorProps) {
  const [open, setOpen] = useState(false);
  const active = sidechainTrackId != null;
  const sourceTrack = trackList.find(t => t.id === sidechainTrackId);

  return (
    <div className={scRow}>
      <span
        className={cn(scBadge, active ? scBadgeActive : scBadgeIdle)}
        title="Sidechain compressor"
      >
        SC
      </span>

      {active && (
        <input
          type="range"
          min={0} max={1} step={0.01}
          value={sidechainSensitivity}
          onChange={e => useMixerStore.getState().setSidechainSensitivity(trackId, parseFloat(e.target.value))}
          className={scSlider}
          title={`Sensitivity: ${Math.round(sidechainSensitivity * 100)}%`}
        />
      )}

      <button
        onClick={() => setOpen(!open)}
        className={cn(scNameBtn, active ? scNameActive : scNameIdle)}
        title={active ? `SC from: ${sourceTrack?.name ?? '?'}` : 'Select sidechain source'}
      >
        {active ? (sourceTrack?.name ?? '?') : '—'}
      </button>

      {open && (
        <div className={dropdown}>
          <button
            onClick={() => { useMixerStore.getState().setSidechainSource(trackId, null); setOpen(false); }}
            className={dropdownItemNone}
          >
            — None —
          </button>
          {trackList.map(t => (
            <button
              key={t.id}
              onClick={() => { useMixerStore.getState().setSidechainSource(trackId, t.id); setOpen(false); }}
              className={cn(dropdownItem, t.id === sidechainTrackId && dropdownItemSelected)}
            >
              {t.name}
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
  fx: PluginSlot;
  channelStrip: PluginSlot;
  isMaster?: boolean;
  sidechainTrackId?: number | null;
  sidechainSensitivity?: number;
  trackList?: Track[];
}

export function PluginSlots({ trackId, trackType, instrument, fx, channelStrip, isMaster, sidechainTrackId, sidechainSensitivity, trackList }: PluginSlotsProps) {
  const availableInstruments = useMixerStore((s) => s.availableInstruments);
  const availableFx = useMixerStore((s) => s.availableFx);
  const availableEffects = useMixerStore((s) => s.availableEffects);

  return (
    <div className={slotsContainer}>
      {trackType === 'midi' ? (
        <PluginSlotButton
          slot={instrument}
          label="+ Instrument"
          icon="🎹"
          options={availableInstruments}
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
      ) : (
        <div className="h-4" />
      )}
      <PluginSlotButton
        slot={fx}
        label="+ FX"
        icon={isMaster ? "🎛️" : "✨"}
        options={availableFx}
        onSelect={(pluginId, pluginName) =>
          useMixerStore.getState().setFx(trackId, pluginId, pluginName)
        }
        onToggleBypass={() =>
          useMixerStore.getState().toggleFxBypass(trackId)
        }
        onOpen={() =>
          useMixerStore.getState().openPlugin(trackId, 'fx')
        }
      />
      <PluginSlotButton
        slot={channelStrip}
        label="+ Strip"
        icon="🎛️"
        options={availableEffects}
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

      {/* Sidechain selector — shown when a channel strip is loaded and not on master/return */}
      {channelStrip.pluginId && !isMaster && (
        <SidechainSelector
          trackId={trackId}
          sidechainTrackId={sidechainTrackId ?? null}
          sidechainSensitivity={sidechainSensitivity ?? 0.6}
          trackList={(trackList ?? []).filter(t => t.id !== trackId && !t.isReturn && !t.isMaster)}
        />
      )}
    </div>
  );
}

// --- Style constants ---

const slotsContainer = `w-full px-1.5 space-y-0.5 mb-1.5 overflow-visible`;

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
  w-4 h-4 shrink-0 rounded flex items-center justify-center
  text-[9px] leading-none
  text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]
  hover:bg-[hsl(var(--muted))] transition-colors`;

const dropdown = `
  absolute top-5 left-0 z-[100] w-36 max-h-40
  overflow-y-auto bg-[hsl(var(--card))] border border-[hsl(var(--border))] rounded shadow-xl`;

const dropdownItemNone = `
  w-full text-left px-2 py-1 text-[9px]
  text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--muted))] hover:text-[hsl(var(--foreground))]`;

const dropdownItem = `
  w-full text-left px-2 py-1 text-[9px]
  text-[hsl(var(--foreground))] hover:bg-[hsl(var(--muted))] transition-colors`;
const dropdownItemSelected = `text-[hsl(var(--progress))] bg-[hsl(var(--muted))]/50`;

// --- Sidechain selector styles ---
const scRow = `relative flex items-center gap-0.5 mt-0.5`;
const scBadge = `text-[7px] font-bold px-1 rounded shrink-0 transition-colors`;
const scBadgeActive = `bg-amber-500/20 text-amber-300`;
const scBadgeIdle = `bg-[hsl(var(--muted))]/30 text-[hsl(var(--muted-foreground))]`;
const scNameBtn = `flex-1 h-4 rounded text-[7px] truncate px-1 text-left transition-colors`;
const scNameActive = `bg-amber-500/10 text-amber-300 hover:bg-amber-500/20`;
const scNameIdle = `bg-[hsl(var(--card))]/40 text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--card))]`;
const scSlider = `
  w-14 h-1.5 shrink-0 appearance-none cursor-pointer rounded-full
  bg-amber-500/20
  [&::-webkit-slider-thumb]:appearance-none
  [&::-webkit-slider-thumb]:w-2.5
  [&::-webkit-slider-thumb]:h-2.5
  [&::-webkit-slider-thumb]:rounded-full
  [&::-webkit-slider-thumb]:bg-amber-400
  [&::-webkit-slider-thumb]:cursor-pointer
  [&::-webkit-slider-thumb]:transition-colors
  [&::-webkit-slider-thumb]:hover:bg-amber-300`;
