function refresh() {
  fetch("/debug")
    .then(res => res.json())
    .then(data => {
      const table = document.getElementById("debugTable");
      table.innerHTML = "";

      for (const key in data) {
        const row = document.createElement("tr");

        const k = document.createElement("td");
        k.textContent = key;

        const v = document.createElement("td");
        v.textContent = data[key];

        row.appendChild(k);
        row.appendChild(v);
        table.appendChild(row);
      }
    })
    .catch(() => {
      alert("Erreur récupération debug");
    });
}

refresh();
