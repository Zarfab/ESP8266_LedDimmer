const slider = document.getElementById("slider");

fetch("/api/pwm")
  .then(r => r.json())
  .then(j => slider.value = j.pwm);

slider.oninput = () => {
  fetch("/api/pwm", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: "value=" + slider.value
  });
};

document.getElementById("saveBtn").addEventListener("click", () => {
  fetch("/api/save", {
		method: "POST",
		headers: { "Content-Type": "application/x-www-form-urlencoded" }
	  })
    .then(response => response.text())
    .then(data => {
      document.getElementById("saveStatus").innerText = data;
    })
    .catch(err => {
      document.getElementById("saveStatus").innerText = "Erreur sauvegarde";
    });
});