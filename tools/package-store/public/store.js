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

const keyForm = document.getElementById('key-form');
if (keyForm) {
  const keyInput = document.getElementById('publishing-key');
  const keyFile = document.getElementById('publishing-key-file');
  const form = document.getElementById('publish-form');
  const unlock = document.getElementById('publish-unlock');
  const status = document.getElementById('publish-status');
  const mode = document.getElementById('publish-mode');
  const picker = document.getElementById('listing-package');
  const button = document.getElementById('publish-submit');
  const previewOptions = document.getElementById('existing-preview-options');
  let listing = null;
  let loadGeneration = 0;
  let key = '';
  const lock = () => {
    ++loadGeneration;
    key = ''; keyInput.value = ''; keyFile.value = '';
    form.hidden = true; unlock.hidden = false;
  };
  const message = text => { status.textContent = text; };
  const updateMode = () => {
    ++loadGeneration; listing = null;
    const editing = mode.value === 'edit';
    document.getElementById('archive-field').hidden = editing;
    document.getElementById('archive').required = !editing;
    document.getElementById('listing-picker').hidden = !editing;
    document.getElementById('publish-heading').textContent = editing ? 'Edit listing' : 'New release';
    button.textContent = editing ? 'Save listing' : 'Publish package ↑';
    button.disabled = editing;
    picker.value = '';
    document.getElementById('listing-identity').textContent = '';
    previewOptions.replaceChildren(); document.getElementById('existing-previews').hidden = true;
    for (const name of ['title','description','publisher','tags']) form.elements[name].value = '';
    document.getElementById('publish-images').value = '';
    document.getElementById('publish-image-links').value = '';
  };
  mode.addEventListener('change', updateMode);
  picker.addEventListener('change', async () => {
    const generation = ++loadGeneration;
    listing = null; button.disabled = true;
    if (!picker.value) return;
    message('Loading listing…');
    try {
      const response = await fetch('/api/publishing/listings/'+encodeURIComponent(picker.value), {
        headers:{Authorization:'Bearer '+key}, credentials:'omit', cache:'no-store'
      });
      const data = await response.json();
      if (generation !== loadGeneration) return;
      if (!response.ok) {
        if (response.status === 401) lock();
        throw new Error(data.error || 'Could not load listing.');
      }
      listing = data;
      for (const name of ['title','description','publisher','license','kind','category']) form.elements[name].value = data.record[name];
      form.elements.tags.value = data.record.tags.join(', ');
      document.getElementById('publish-images').value = '';
      document.getElementById('publish-image-links').value = '';
      previewOptions.replaceChildren();
      for (const [index, url] of data.record.images.entries()) {
        if (url === '/images/demi-asset.svg') continue;
        const label = document.createElement('label'); label.className = 'existing-preview';
        const checkbox = document.createElement('input'); checkbox.type = 'checkbox'; checkbox.checked = true; checkbox.value = url;
        const image = document.createElement('img'); image.src = url; image.alt = ''; image.loading = 'lazy';
        label.append(checkbox, image, document.createTextNode('Keep preview '+(index+1)));
        previewOptions.append(label);
      }
      document.getElementById('existing-previews').hidden = !previewOptions.childElementCount;
      document.getElementById('listing-identity').textContent = data.record.manifest.name+'@'+data.record.manifest.version+' — archive, version and release date stay unchanged.';
      button.disabled = false; message('Listing loaded. Make your changes, then save.');
    } catch (error) { if (generation === loadGeneration) message(error.message); }
  });
  keyFile.addEventListener('change', async () => {
    const file = keyFile.files[0];
    if (file && file.size < 1024) keyInput.value = (await file.text()).trim();
    else message('Select your publishing key file.');
  });
  document.getElementById('lock-publishing').addEventListener('click', () => {
    lock(); message('Publishing locked.'); keyInput.focus();
  });
  window.addEventListener('pagehide', lock);
  keyForm.addEventListener('submit', async event => {
    event.preventDefault();
    key = keyInput.value.trim();
    message('Checking access…');
    try {
      const response = await fetch('/api/publishing/check', {
        method:'POST', headers:{Authorization:'Bearer '+key}, credentials:'omit'
      });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || 'Could not verify publishing access.');
      const catalogResponse = await fetch('/v1/catalog', {cache:'no-store'});
      if (!catalogResponse.ok) throw new Error('Could not load the package list.');
      const catalog = await catalogResponse.json();
      picker.replaceChildren(new Option('Choose a package…',''));
      for (const record of catalog.packages) picker.add(new Option(record.title+' — '+record.manifest.name, record.manifest.name));
      mode.value = 'new'; updateMode();
      keyInput.value = ''; keyFile.value = '';
      unlock.hidden = true; form.hidden = false;
      message('Publisher access unlocked.'); document.getElementById('archive').focus();
    } catch (error) { lock(); message(error.message); }
  });
  form.addEventListener('submit', async event => {
    event.preventDefault();
    const archive = document.getElementById('archive').files[0];
    const files = Array.from(document.getElementById('publish-images').files);
    const links = document.getElementById('publish-image-links').value.split(/\r?\n/).map(s=>s.trim()).filter(Boolean);
    const editing = mode.value === 'edit';
    const kept = editing ? Array.from(previewOptions.querySelectorAll('input:checked'), input=>input.value) : [];
    if (editing && !listing) { message('Choose a listing first.'); return; }
    if (!editing && (!archive || archive.size > 512*1024*1024)) { message('Select a package archive up to 512 MB.'); return; }
    if (kept.length+files.length+links.length>8 || files.some(file=>file.size>10*1024*1024)) {
      message('Use at most eight images, with uploaded images no larger than 10 MB.'); return;
    }
    if (new Set(files.map(file=>file.name)).size !== files.length) {
      message('Preview filenames must be unique.'); return;
    }
    const metadata = {format_version:1,price:0};
    for (const name of ['title','description','publisher','license','kind','category']) metadata[name] = form.elements[name].value.trim();
    metadata.tags = form.elements.tags.value.split(',').map(s=>s.trim()).filter(Boolean);
    metadata.images = [...kept,...files.map(file=>file.name),...links];
    const body = new FormData();
    if (editing) {
      body.append('name',listing.record.manifest.name); body.append('version',listing.record.manifest.version);
      body.append('revision',listing.revision);
    } else body.append('archive',archive);
    body.append('metadata',JSON.stringify(metadata));
    for (const file of files) body.append('images[]',file);
    button.disabled = true; mode.disabled = true; picker.disabled = true;
    message(editing ? 'Saving listing…' : 'Uploading and validating the release…');
    try {
      const response = await fetch('/api/publishing/'+(editing ? 'listings' : 'releases'), {
        method:'POST', headers:{Authorization:'Bearer '+key}, body, credentials:'omit'
      });
      const data = await response.json().catch(()=>({error:'Upload failed. Please check the archive size and retry.'}));
      if (!response.ok) {
        if (response.status===401) lock();
        throw new Error(data.error || 'Publication failed.');
      }
      form.reset(); lock(); message((editing ? 'Updated ' : 'Published ')+data.name+'@'+data.version+'. ');
      const link = document.createElement('a'); link.href = data.url; link.textContent = 'View package →';
      status.appendChild(link);
    } catch (error) { message(error.message); }
    finally { button.disabled = false; mode.disabled = false; picker.disabled = false; }
  });
}
