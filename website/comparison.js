// Select pre-generated responses; no browser-side DSP or plotting library.
const controls = document.querySelector('#comparison-controls');
const axisButtons = controls.querySelectorAll('[data-comparison-axis]');
const typeButtons = controls.querySelectorAll('[data-comparison-type]');
const slopeControls = document.querySelector('#comparison-slope-controls');
const slopeSelector = document.querySelector('#comparison-slope');
const image = document.querySelector('#comparison-plot');
const settings = document.querySelector('#comparison-settings');
const dataLink = document.querySelector('#comparison-data');
const scrollRegion = document.querySelector('#comparison-scroll');
const filterNames = {
    bell: 'Bell',
    'low-shelf': 'Low shelf',
    'high-shelf': 'High shelf',
    'low-cut': 'Low cut / high-pass',
    'high-cut': 'High cut / low-pass',
};
let axis = 'hz';
let filter = 'bell';

function updateComparison() {
    const cut = filter === 'low-cut' || filter === 'high-cut';
    const stem = cut ? `${filter}-${slopeSelector.value}db` : filter;
    const name = filterNames[filter];
    for (const button of axisButtons) {
        button.setAttribute('aria-pressed', String(button.dataset.comparisonAxis === axis));
    }
    for (const button of typeButtons) {
        button.setAttribute('aria-pressed', String(button.dataset.comparisonType === filter));
    }
    slopeControls.hidden = !cut;
    image.src = `assets/eq-comparison/${stem}-comparison-${axis}.svg`;
    image.parentElement.href = image.src;
    const configuration = cut
        ? `${slopeSelector.value} dB/octave · ${slopeSelector.value === '6' ? 'Q inactive' : 'Q = 1/√2'} · Butterworth prototype`
        : 'Q = 1 · ±12 dB';
    image.alt = `${name} response comparison at 48 kHz, ${configuration}, with errors stacked by centre. `
        + (axis === 'hz' ? 'Frequency in Hz.' : 'Frequency relative to Nyquist.');
    scrollRegion.setAttribute('aria-label', `${name} comparison plot, scroll horizontally on smaller screens`);
    settings.textContent = `${name} · 48 kHz · ${configuration} · centres at 1.2, 12 and 19.2 kHz. `
        + 'Each curve represents an independent band, not their combined response. Error panels are stacked by centre. '
        + (cut ? 'Magnitude is shown down to −120 dB; errors outside ±60 dB are clipped. '
            + (filter === 'high-cut' ? 'The exact JUCE/BLT zero at Nyquist (−∞ dB) is omitted from the error lines. ' : '')
            : 'Solid error lines show boosts and dashed lines show cuts. ')
        + 'JUCE and BLT share an error trace because they overlap.';
    dataLink.href = `assets/eq-comparison/${stem}-curves.csv`;
    dataLink.textContent = `${name}${cut ? ` (${slopeSelector.value} dB/octave)` : ''} response data (CSV)`;
}

for (const button of axisButtons) {
    button.addEventListener('click', () => {
        axis = button.dataset.comparisonAxis;
        updateComparison();
    });
}
for (const button of typeButtons) {
    button.addEventListener('click', () => {
        filter = button.dataset.comparisonType;
        updateComparison();
    });
}
slopeSelector.addEventListener('change', updateComparison);
updateComparison();
controls.hidden = false;
