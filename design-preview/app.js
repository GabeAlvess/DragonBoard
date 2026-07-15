const navItems = [...document.querySelectorAll('.nav-item')];
const pages = [...document.querySelectorAll('.page')];
const breadcrumb = document.querySelector('#breadcrumb-current');
const saveState = document.querySelector('#save-state');
const saveButton = document.querySelector('#save-button');

function showPage(name) {
  navItems.forEach(item => item.classList.toggle('active', item.dataset.page === name));
  pages.forEach(page => page.classList.toggle('active', page.id === `page-${name}`));
  const active = pages.find(page => page.id === `page-${name}`);
  breadcrumb.textContent = active?.dataset.title || name;
}

function setRangeFill(input) {
  const min = Number(input.min || 0);
  const max = Number(input.max || 100);
  const value = Number(input.value);
  input.style.setProperty('--fill', `${((value - min) / (max - min)) * 100}%`);
}

navItems.forEach(item => item.addEventListener('click', () => showPage(item.dataset.page)));

document.querySelectorAll('input[type="range"]').forEach(input => {
  setRangeFill(input);
  input.addEventListener('input', () => {
    setRangeFill(input);
    const output = input.closest('.control, .axis-card')?.querySelector('output, strong');
    if (output) {
      const suffix = output.textContent.includes('°') ? '°' : output.textContent.includes('×') ? '×' : '';
      const decimals = Number(input.step) > 0 && Number(input.step) < 1 ? 2 : 1;
      output.textContent = `${Number(input.value).toFixed(decimals)}${suffix}`;
    }
    saveState.textContent = 'Unsaved changes';
    saveState.classList.add('dirty');
  });
});

saveButton.addEventListener('click', () => {
  saveState.textContent = 'All changes saved';
  saveState.classList.remove('dirty');
  saveButton.animate([
    { transform: 'scale(1)' },
    { transform: 'scale(.97)' },
    { transform: 'scale(1)' }
  ], { duration: 180 });
});
