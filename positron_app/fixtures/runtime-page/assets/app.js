if (document.getElementById('status').textContent === 'inline-ready' && typeof PositronServices === 'undefined') {
    document.getElementById('status').textContent = 'directory-script-ready';
}
