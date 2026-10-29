const form = document.querySelector("#studentForm");
const rows = document.querySelector("#studentRows");
const searchInput = document.querySelector("#searchInput");
const notice = document.querySelector("#notice");
let students = [];

function showNotice(message, type = "success") {
  notice.textContent = message;
  notice.className = `notice ${type}`;
  notice.hidden = false;
  window.clearTimeout(showNotice.timer);
  showNotice.timer = window.setTimeout(() => { notice.hidden = true; }, 4500);
}

async function api(url, options = {}) {
  const response = await fetch(url, options);
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || "Request failed");
  return data;
}

function renderStudents() {
  const query = searchInput.value.trim().toLowerCase();
  const filtered = students.filter(s =>
    s.reg_no.toLowerCase().includes(query) || s.name.toLowerCase().includes(query)
  );
  rows.replaceChildren();
  if (!filtered.length) {
    const tr = document.createElement("tr");
    const td = document.createElement("td");
    td.colSpan = 5;
    td.className = "empty";
    td.textContent = query ? "No matching students found." : "No student records yet. Add your first student above.";
    tr.appendChild(td);
    rows.appendChild(tr);
  } else {
    filtered.forEach(s => {
      const tr = document.createElement("tr");
      const reg = document.createElement("td"); reg.className = "reg"; reg.textContent = s.reg_no;
      const name = document.createElement("td"); name.textContent = s.name;
      const marks = document.createElement("td"); marks.className = "mark"; marks.textContent = Number(s.marks).toFixed(2);
      const result = document.createElement("td");
      const badge = document.createElement("span");
      badge.className = `result ${Number(s.marks) >= 35 ? "pass" : "fail"}`;
      badge.textContent = Number(s.marks) >= 35 ? "PASS" : "FAIL";
      result.appendChild(badge);
      const action = document.createElement("td");
      const del = document.createElement("button"); del.className = "delete"; del.type = "button"; del.textContent = "Delete";
      del.addEventListener("click", () => deleteStudent(s.reg_no));
      action.appendChild(del);
      tr.append(reg, name, marks, result, action);
      rows.appendChild(tr);
    });
  }
  document.querySelector("#totalStudents").textContent = students.length;
  document.querySelector("#averageMarks").textContent = students.length
    ? (students.reduce((sum, s) => sum + Number(s.marks), 0) / students.length).toFixed(1)
    : "—";
  document.querySelector("#passingStudents").textContent = students.filter(s => Number(s.marks) >= 35).length;
  document.querySelector("#recordCount").textContent = `Showing ${filtered.length} of ${students.length} student(s)`;
}

async function loadStudents() {
  try {
    const data = await api("/api/students");
    students = data.students;
    renderStudents();
  } catch (err) {
    showNotice(`${err.message}. Is the C server running?`, "error");
    rows.innerHTML = '<tr><td colspan="5" class="empty">Could not load records. Check that the C server is running.</td></tr>';
  }
}

form.addEventListener("submit", async event => {
  event.preventDefault();
  const values = new FormData(form);
  const body = new URLSearchParams();
  body.set("reg_no", values.get("reg_no").trim());
  body.set("name", values.get("name").trim());
  body.set("marks", values.get("marks"));
  try {
    const data = await api("/api/students", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body
    });
    form.reset();
    showNotice(data.message);
    await loadStudents();
  } catch (err) {
    showNotice(err.message, "error");
  }
});

async function deleteStudent(regNo) {
  if (!window.confirm(`Delete student ${regNo}? This cannot be undone.`)) return;
  try {
    const data = await api(`/api/students/${encodeURIComponent(regNo)}`, { method: "DELETE" });
    showNotice(data.message);
    await loadStudents();
  } catch (err) {
    showNotice(err.message, "error");
  }
}

searchInput.addEventListener("input", renderStudents);
document.querySelector("#refreshBtn").addEventListener("click", loadStudents);
loadStudents();
