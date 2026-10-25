const API = "/API/animations";

const listEl = document.getElementById("animation-list");
const statusEl = document.getElementById("status");
const refreshButton = document.getElementById("refresh-button");
const uploadButton = document.getElementById("upload-button");
const uploadInput = document.getElementById("upload-input");

function setStatus(message, isError = false) {
    statusEl.textContent = message;
    statusEl.classList.toggle("error", isError);
}

async function describeError(response) {
    try {
        const problem = await response.json();
        return problem.detail || problem.title || `HTTP ${response.status}`;
    } catch {
        return `HTTP ${response.status}`;
    }
}

function makeButton(label, onClick, className) {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = label;
    if (className) {
        button.className = className;
    }
    button.addEventListener("click", onClick);
    return button;
}

function render(animations) {
    listEl.replaceChildren();

    if (animations.length === 0) {
        const row = listEl.insertRow();
        const cell = row.insertCell();
        cell.colSpan = 4;
        cell.className = "empty";
        cell.textContent = "No animations";
        return;
    }

    animations.sort((a, b) => String(a.Name).localeCompare(String(b.Name)));

    for (const animation of animations) {
        const row = listEl.insertRow();

        const nameCell = row.insertCell();
        nameCell.className = "name";
        nameCell.textContent = animation.Name;

        row.insertCell().textContent = Array.isArray(animation.Frames) ? animation.Frames.length : "?";
        row.insertCell().textContent = animation.FrameRate ?? "?";

        const actions = row.insertCell();
        actions.className = "actions";
        actions.append(
            makeButton("Run", () => runAnimation(animation.Name)),
            makeButton("Edit", () => {
                location.href = `editor.html?name=${encodeURIComponent(animation.Name)}`;
            }),
            makeButton("Download", () => downloadAnimation(animation.Name)),
            makeButton("Delete", () => deleteAnimation(animation.Name), "danger")
        );
    }
}

async function refresh() {
    refreshButton.disabled = true;
    setStatus("Loading...");
    try {
        const response = await fetch(API);
        if (!response.ok) {
            throw new Error(await describeError(response));
        }
        const animations = await response.json();
        render(animations);
        setStatus(`${animations.length} animation(s)`);
    } catch (error) {
        setStatus(`Failed to load animations: ${error.message}`, true);
    } finally {
        refreshButton.disabled = false;
    }
}

async function runAnimation(name) {
    try {
        const response = await fetch(`${API}/${encodeURIComponent(name)}/run`, { method: "POST" });
        if (!response.ok) {
            throw new Error(await describeError(response));
        }
        setStatus(`Running "${name}"`);
    } catch (error) {
        setStatus(`Failed to run "${name}": ${error.message}`, true);
    }
}

async function downloadAnimation(name) {
    try {
        const response = await fetch(`${API}/${encodeURIComponent(name)}`);
        if (!response.ok) {
            throw new Error(await describeError(response));
        }
        const blob = new Blob([JSON.stringify(await response.json(), null, 2)], { type: "application/json" });
        const url = URL.createObjectURL(blob);
        const link = document.createElement("a");
        link.href = url;
        link.download = `${name}.json`;
        link.click();
        URL.revokeObjectURL(url);
    } catch (error) {
        setStatus(`Failed to download "${name}": ${error.message}`, true);
    }
}

async function deleteAnimation(name) {
    if (!confirm(`Delete animation "${name}"?`)) {
        return;
    }
    try {
        const response = await fetch(`${API}/${encodeURIComponent(name)}`, { method: "DELETE" });
        if (!response.ok) {
            throw new Error(await describeError(response));
        }
        await refresh();
        setStatus(`Deleted "${name}"`);
    } catch (error) {
        setStatus(`Failed to delete "${name}": ${error.message}`, true);
    }
}

async function uploadFiles(files) {
    const failures = [];
    let succeeded = 0;

    for (const file of files) {
        try {
            const animation = JSON.parse(await file.text());
            const response = await fetch(API, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(animation),
            });
            if (!response.ok) {
                throw new Error(await describeError(response));
            }
            succeeded++;
        } catch (error) {
            failures.push(`${file.name}: ${error.message}`);
        }
    }

    await refresh();
    if (failures.length > 0) {
        setStatus(`Uploaded ${succeeded}, failed ${failures.length}. ${failures.join("; ")}`, true);
    } else {
        setStatus(`Uploaded ${succeeded} animation(s)`);
    }
}

refreshButton.addEventListener("click", refresh);
uploadButton.addEventListener("click", () => uploadInput.click());
uploadInput.addEventListener("change", async () => {
    const files = Array.from(uploadInput.files);
    uploadInput.value = "";
    if (files.length > 0) {
        await uploadFiles(files);
    }
});

refresh();
