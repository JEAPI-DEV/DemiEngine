document.querySelectorAll('img[data-fallback]').forEach(img => {
  const fallback = () => { img.onerror = null; if (!img.src.endsWith(img.dataset.fallback)) img.src = img.dataset.fallback; };
  img.addEventListener('error', fallback, {once:true});
  if (img.complete && img.naturalWidth === 0) fallback();
});
document.querySelectorAll('[data-gallery]').forEach(button => button.addEventListener('click', () => {
  const image = document.getElementById('gallery-image');
  image.src = button.dataset.gallery;
  image.onerror = () => { image.onerror = null; image.src = '/images/demi-asset.svg'; };
}));
document.querySelectorAll('[data-copy]').forEach(button => button.addEventListener('click', async () => {
  const code = document.getElementById(button.dataset.copy);
  const status = document.getElementById('copy-status');
  try {
    await navigator.clipboard.writeText(code.textContent);
    status.textContent = 'Copied.';
  } catch {
    const range = document.createRange(); range.selectNodeContents(code);
    const selection = window.getSelection(); selection.removeAllRanges(); selection.addRange(range);
    status.textContent = 'Command selected. Press Ctrl+C to copy.';
  }
}));

