import { useLayoutEffect, useRef, useState, type CSSProperties, type KeyboardEvent } from 'react';
import { displayStatus, type Display, type IdentifyState } from './displayData';

const SELECTED_SCALE = 1.2;
const modulo = (value: number, count: number) => ((value % count) + count) % count;

export function DisplayStatus({ display }: { display: Display }) {
  const active = display.identityStatus === 'ready' && display.sessionStatus === 'ready';
  return (
    <span className={'display-status' + (active ? ' display-status--active' : '')}>
      <span className="status-dot" aria-hidden="true" />{displayStatus(display)}
    </span>
  );
}

interface DisplayDeckProps {
  displays: readonly Display[];
  position: number;
  onPositionChange: (update: (current: number) => number) => void;
  identifyStates: Record<string, IdentifyState>;
  onIdentify: (id: string) => void;
}

export function DisplayDeck({ displays, position, onPositionChange, identifyStates, onIdentify }: DisplayDeckProps) {
  const viewport = useRef<HTMLDivElement>(null);
  const focusAfterMove = useRef(false);
  const [viewportWidth, setViewportWidth] = useState(880);
  const count = displays.length;
  const selectedIndex = modulo(position, count);
  const cycle = Math.floor(position / count);

  useLayoutEffect(() => {
    const element = viewport.current;
    if (!element) return;
    const measure = () => setViewportWidth(element.getBoundingClientRect().width);
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(element);
    return () => observer.disconnect();
  }, []);

  useLayoutEffect(() => {
    if (!focusAfterMove.current) return;
    focusAfterMove.current = false;
    viewport.current?.querySelector<HTMLButtonElement>('[data-selected="true"]')?.focus({ preventScroll: true });
  }, [position]);

  // Size responds to the window, never to the number of connected displays.
  const largestAspect = Math.max(...displays.map(display => display.resolution.width / display.resolution.height));
  const edgeRoom = Math.max(24, Math.min(72, viewportWidth * .06)) + 12;
  const slideHeight = Math.min(176, Math.max(100, (viewportWidth - edgeRoom * 2) / (largestAspect * SELECTED_SCALE)));
  const gap = viewportWidth < 600 ? 24 : 32;
  const widths = displays.map(display => slideHeight * display.resolution.width / display.resolution.height);
  const centers: number[] = [];
  let period = 0;
  for (const width of widths) { centers.push(period + width / 2); period += width + gap; }
  const centerOf = (index: number) => Math.floor(index / count) * period + centers[modulo(index, count)];
  const selectedCenter = centerOf(position);
  const extraSpace = widths[selectedIndex] * (SELECTED_SCALE - 1) / 2;

  // A bounded window of stable, repeated slides follows an unbounded visual
  // index. Last -> first is +1, never a rewind or a visible transform reset.
  const slots = count === 1 ? [0] : Array.from({ length: count * 5 }, (_, index) => (cycle - 2) * count + index);
  const accessibleInstance = (logicalIndex: number) => {
    const candidates = [-1, 0, 1].map(offset => (cycle + offset) * count + logicalIndex);
    return candidates.reduce((best, candidate) =>
      Math.abs(centerOf(candidate) - selectedCenter) <= Math.abs(centerOf(best) - selectedCenter) ? candidate : best);
  };

  function select(update: (current: number) => number, moveFocus = false) {
    const active = document.activeElement;
    const focusedSlide = active instanceof HTMLElement && viewport.current?.contains(active);
    // Retire the old focus before React can mark that repeated instance hidden.
    // Pointer/keyboard focus on navigation controls stays on those controls.
    if (focusedSlide) active.blur();
    focusAfterMove.current ||= moveFocus || Boolean(focusedSlide);
    onPositionChange(update);
  }
  function step(direction: number, moveFocus = false) {
    if (count < 2) return;
    select(current => current + direction, moveFocus);
  }
  function jump(logicalIndex: number) {
    if (logicalIndex !== selectedIndex) select(current => current + logicalIndex - modulo(current, count));
  }
  function onKeyDown(event: KeyboardEvent<HTMLElement>) {
    if (event.altKey || event.ctrlKey || event.metaKey || count < 2) return;
    if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
    event.preventDefault();
    step(event.key === 'ArrowRight' ? 1 : -1, true);
  }

  return (
    <section className="display-deck" aria-label="Display deck" aria-roledescription="carousel"
      onKeyDown={onKeyDown} data-display-count={count} data-position={position}
      style={{ '--slide-height': slideHeight + 'px', '--deck-duration': '220ms' } as CSSProperties}>
      <div className="deck-viewport" ref={viewport}>
        <div className="deck-track" style={{ transform: 'translateX(' + -selectedCenter + 'px)' }}>
          {slots.map(slot => {
            const logicalIndex = modulo(slot, count);
            const display = displays[logicalIndex];
            const selected = slot === position;
            const duplicate = count > 1 && accessibleInstance(logicalIndex) !== slot;
            const portrait = display.resolution.height > display.resolution.width;
            const identifyState = identifyStates[display.id] ?? 'idle';
            const center = centerOf(slot) + (slot < position ? -extraSpace : slot > position ? extraSpace : 0);
            return (
              <div key={slot} className={'display-slide' + (selected ? ' is-focused' : '') + (portrait ? ' is-portrait' : '')}
                aria-hidden={duplicate || undefined}
                style={{ left: center + 'px', width: widths[logicalIndex] + 'px',
                  '--selection-rise': (selected ? slideHeight * (SELECTED_SCALE - 1) / 2 : 0) + 'px' } as CSSProperties}>
                <button type="button" className={'display-surface' + (selected ? ' is-selected' : '')}
                  aria-label={'Select ' + display.name} aria-pressed={selected} tabIndex={selected ? 0 : -1}
                  aria-describedby={'status-slide-' + slot} data-selected={selected} data-visual-index={slot}
                  data-display-id={display.id}
                  onMouseDown={event => { if (duplicate) event.preventDefault(); }}
                  onClick={() => { if (slot !== position) select(() => slot, true); }}>
                  <span className="display-index" aria-hidden="true">0{logicalIndex + 1}</span>
                  {display.primary && <span className="primary-mark">PRIMARY</span>}
                  <span className="display-label">
                    <span className={'display-name' + (selected ? ' display-name--spacer' : '')} aria-hidden={selected || undefined}><span>DISPLAY</span>{' '}<span>{display.name.slice(-1)}</span></span>
                    <span className="display-resolution"><span>{display.resolution.width}</span><span>×</span><span>{display.resolution.height}</span></span>
                  </span>
                  <span className="selection-edge" aria-hidden="true" />
                </button>
                {selected && <div className="display-identify-layer">
                  <div className="display-label">
                    <button type="button" className="display-name display-identify" aria-label={'Identify ' + display.name}
                      aria-describedby="identify-name-tooltip" aria-busy={identifyState === 'identifying'}
                      disabled={display.identityStatus === 'unavailable' || identifyState === 'identifying'}
                      data-identify-state={identifyState} onClick={() => onIdentify(display.id)}>
                      {identifyState === 'identifying' ? 'IDENTIFYING…' : <><span>DISPLAY</span>{' '}<span>{display.name.slice(-1)}</span></>}
                      <span className="control-tooltip" id="identify-name-tooltip" role="tooltip">Identify display</span>
                    </button>
                    <span className="display-resolution display-name--spacer" aria-hidden="true"><span>{display.resolution.width}</span><span>×</span><span>{display.resolution.height}</span></span>
                  </div>
                </div>}
                <div className="display-caption" id={'status-slide-' + slot}>
                  <DisplayStatus display={display} />
                </div>
              </div>
            );
          })}
        </div>
      </div>
      {count > 1 && <div className="deck-navigation" role="group" aria-label="Display navigation">
        <button type="button" className="deck-arrow" aria-label="Previous display" onClick={() => step(-1)}>
          <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="m10 3-5 5 5 5" /></svg>
        </button>
        <div className="deck-indicators" role="group" aria-label="Choose a display">
          {displays.map((display, index) => <button type="button" key={display.id}
            className={'deck-indicator' + (index === selectedIndex ? ' is-current' : '')}
            aria-label={'Go to ' + display.name} aria-current={index === selectedIndex ? 'true' : undefined}
            onClick={() => jump(index)}><span aria-hidden="true" /></button>)}
        </div>
        <button type="button" className="deck-arrow" aria-label="Next display" onClick={() => step(1)}>
          <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="m6 3 5 5-5 5" /></svg>
        </button>
      </div>}
      <span className="sr-only" aria-live="polite" aria-atomic="true">
        {displays[selectedIndex].name}, {selectedIndex + 1} of {count}
      </span>
    </section>
  );
}
