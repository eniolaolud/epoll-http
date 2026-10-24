async function refreshUploads() {
  const ul = document.getElementById("uploads");
  if (!ul) return;
  try {
    const res = await fetch("/api/uploads");
    const data = await res.json();
    ul.innerHTML = "";
    if (!data.files || data.files.length === 0) {
      ul.innerHTML = "<li><em>No uploads yet</em></li>";
      return;
    }
    for (const name of data.files) {
      const li = document.createElement("li");
      const a = document.createElement("a");
      a.href = "/uploads/" + encodeURIComponent(name);
      a.textContent = name;
      const del = document.createElement("button");
      del.type = "button";
      del.textContent = "Delete";
      del.addEventListener("click", async () => {
        const r = await fetch("/api/delete?name=" + encodeURIComponent(name), {
          method: "DELETE",
        });
        const t = await r.text();
        document.getElementById("delete-result").textContent = t;
        refreshUploads();
      });
      li.append(a, del);
      ul.appendChild(li);
    }
  } catch (e) {
    ul.innerHTML = "<li>Could not load uploads</li>";
  }
}

document.getElementById("delete-form")?.addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const name = document.getElementById("delete-name").value.trim();
  const r = await fetch("/api/delete?name=" + encodeURIComponent(name), {
    method: "DELETE",
  });
  document.getElementById("delete-result").textContent = await r.text();
  refreshUploads();
});

refreshUploads();

const params = new URLSearchParams(location.search);
if (params.get("uploaded") === "1") {
  history.replaceState({}, "", "/");
  refreshUploads();
}
