const ANIMATIONS_URL = "/API/animations";
const LIGHTS_URL = "/API/lights";
const PLAYBACK_CUSTOM_URL = "/API/playback/custom";

const FRAME_BYTES = 11;
const SEGMENTS = ["A", "B", "C", "D", "E", "F", "G"];
const COLORS = [
    { name: "red", code: "r" },
    { name: "yellow", code: "y" },
    { name: "green", code: "g" }
];

const $ = (id) => document.getElementById(id);
const statusEl = $("status");
const nameInput = $("name-input");
const rateInput = $("rate-input");
const loadSelect = $("load-select");
const frameStrip = $("frame-strip");
const frameLabel = $("frame-label");
const previewButton = $("preview-button");

let frames = [newFrame()];
let current = 0;
let previewTimer = null;

function newFrame() {
    return new Array(FRAME_BYTES).fill(0);
}

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

// Build the light buttons for each digit.
document.querySelectorAll(".digit").forEach((digit, digitIndex) => {
    SEGMENTS.forEach((segmentName) => {
        const segment = document.createElement("div");
        segment.className = `segment ${segmentName.toLowerCase()}`;

        COLORS.forEach(({ name, code }) => {
            const light = document.createElement("button");
            light.type = "button";
            light.className = `light ${name}`;
            light.dataset.lightId = `${digitIndex}${segmentName}${code}`;
            light.setAttribute(
                "aria-label",
                `Digit ${digitIndex}, segment ${segmentName}, ${name} light`
            );
            light.setAttribute("aria-pressed", "false");
            light.title = light.dataset.lightId;
            segment.appendChild(light);
        });

        digit.appendChild(segment);
    });
});

// Light IDs in serialization order (bit 0 first).
const LIGHT_IDS = [];
(function buildIds() {
    const addDigit = (d) => {
        for (const s of SEGMENTS) {
            for (const { code } of COLORS) {
                LIGHT_IDS.push(`${d}${s}${code}`);
            }
        }
    };
    addDigit(0);
    addDigit(1);
    LIGHT_IDS.push("X0", "X1");
    addDigit(2);
    addDigit(3);
})();

const lightElements = LIGHT_IDS.map((id) =>
    document.querySelector(`[data-light-id="${id}"]`)
);

function drawFrame(frame) {
    lightElements.forEach((light, bit) => {
        const isOn = ((frame[bit >> 3] >> (bit & 7)) & 1) === 1;
        light.classList.toggle("on", isOn);
        light.setAttribute("aria-pressed", String(isOn));
    });
}

function renderStrip() {
    frameStrip.replaceChildren();

    frames.forEach((_, index) => {
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = String(index + 1);
        button.setAttribute("role", "option");
        button.setAttribute("aria-selected", String(index === current));
        button.classList.toggle("selected", index === current);
        button.addEventListener("click", () => selectFrame(index));
        frameStrip.appendChild(button);
    });

    frameLabel.textContent = `Frame ${current + 1} of ${frames.length}`;
    $("delete-button").disabled = frames.length <= 1;
    $("left-button").disabled = current === 0;
    $("right-button").disabled = current === frames.length - 1;
}

function selectFrame(index) {
    current = index;
    drawFrame(frames[current]);
    renderStrip();
}

lightElements.forEach((light, bit) => {
    light.addEventListener("click", () => {
        stopPreview();
        frames[current][bit >> 3] ^= 1 << (bit & 7);
        drawFrame(frames[current]);
    });
});

$("add-button").addEventListener("click", () => {
    stopPreview();
    frames.splice(current + 1, 0, newFrame());
    selectFrame(current + 1);
});

$("duplicate-button").addEventListener("click", () => {
    stopPreview();
    frames.splice(current + 1, 0, [...frames[current]]);
    selectFrame(current + 1);
});

$("delete-button").addEventListener("click", () => {
    if (frames.length <= 1) {
        return;
    }
    stopPreview();
    frames.splice(current, 1);
    selectFrame(Math.min(current, frames.length - 1));
});

$("clear-button").addEventListener("click", () => {
    stopPreview();
    frames[current] = newFrame();
    drawFrame(frames[current]);
});

$("fill-button").addEventListener("click", () => {
    stopPreview();
    LIGHT_IDS.forEach((_, bit) => {
        frames[current][bit >> 3] |= 1 << (bit & 7);
    });
    drawFrame(frames[current]);
});

$("invert-button").addEventListener("click", () => {
    stopPreview();
    LIGHT_IDS.forEach((_, bit) => {
        frames[current][bit >> 3] ^= 1 << (bit & 7);
    });
    drawFrame(frames[current]);
});

$("left-button").addEventListener("click", () => {
    if (current > 0) {
        stopPreview();
        [frames[current - 1], frames[current]] = [frames[current], frames[current - 1]];
        selectFrame(current - 1);
    }
});

$("right-button").addEventListener("click", () => {
    if (current < frames.length - 1) {
        stopPreview();
        [frames[current + 1], frames[current]] = [frames[current], frames[current + 1]];
        selectFrame(current + 1);
    }
});

function getFrameRate() {
    const rate = parseFloat(rateInput.value);
    return Number.isFinite(rate) && rate > 0 ? rate : null;
}

function buildAnimation() {
    return {
        Name: nameInput.value.trim(),
        FrameRate: getFrameRate(),
        Frames: frames.map((frame) => [...frame])
    };
}

// Returns an error message, or null when the animation is valid.
function validate(animation) {
    if (!animation.Name) {
        return "Enter an animation name";
    }
    if (animation.FrameRate === null) {
        return "Enter a frame rate greater than 0";
    }
    return null;
}

function stopPreview() {
    if (previewTimer !== null) {
        clearInterval(previewTimer);
        previewTimer = null;
        previewButton.textContent = "Preview";
        drawFrame(frames[current]);
    }
}

previewButton.addEventListener("click", () => {
    if (previewTimer !== null) {
        stopPreview();
        return;
    }

    const rate = getFrameRate();
    if (rate === null) {
        setStatus("Enter a frame rate greater than 0", true);
        return;
    }

    let index = 0;
    previewButton.textContent = "Stop preview";
    drawFrame(frames[0]);
    previewTimer = setInterval(() => {
        index = (index + 1) % frames.length;
        drawFrame(frames[index]);
    }, 1000 / rate);
});

async function post(url, body) {
    const response = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body)
    });
    if (!response.ok) {
        throw new Error(await describeError(response));
    }
    return response;
}

$("send-frame-button").addEventListener("click", async () => {
    try {
        await post(LIGHTS_URL, { outputs: frames[current] });
        setStatus(`Frame ${current + 1} sent to clock`);
    } catch (error) {
        setStatus(`Failed to send frame: ${error.message}`, true);
    }
});

$("play-button").addEventListener("click", async () => {
    const animation = buildAnimation();
    const problem = validate(animation);
    if (problem) {
        setStatus(problem, true);
        return;
    }
    try {
        await post(PLAYBACK_CUSTOM_URL, animation);
        setStatus("Animation playing on clock");
    } catch (error) {
        setStatus(`Failed to play animation: ${error.message}`, true);
    }
});

$("save-button").addEventListener("click", async () => {
    const animation = buildAnimation();
    const problem = validate(animation);
    if (problem) {
        setStatus(problem, true);
        return;
    }
    try {
        const response = await fetch(ANIMATIONS_URL, {
            method: "PUT",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(animation)
        });
        if (!response.ok) {
            throw new Error(await describeError(response));
        }
        setStatus(
            response.status === 201
                ? `Created "${animation.Name}"`
                : `Saved "${animation.Name}"`
        );
        await refreshList(animation.Name);
    } catch (error) {
        setStatus(`Failed to save: ${error.message}`, true);
    }
});

function loadAnimation(animation) {
    stopPreview();
    const loaded = Array.isArray(animation.Frames)
        ? animation.Frames.map((frame) => {
            const bytes = newFrame();
            for (let i = 0; i < FRAME_BYTES; i++) {
                bytes[i] = (Array.isArray(frame) ? frame[i] : 0) & 0xFF;
            }
            return bytes;
        })
        : [];

    frames = loaded.length > 0 ? loaded : [newFrame()];
    nameInput.value = animation.Name ?? "";
    rateInput.value = animation.FrameRate ?? 10;
    selectFrame(0);
}

async function refreshList(selectName) {
    try {
        const response = await fetch(ANIMATIONS_URL);
        if (!response.ok) {
            throw new Error(await describeError(response));
        }
        const animations = await response.json();
        const names = animations.map((a) => String(a.Name))
            .sort((a, b) => a.localeCompare(b));

        loadSelect.replaceChildren(...names.map((name) => new Option(name, name)));
        if (selectName && names.includes(selectName)) {
            loadSelect.value = selectName;
        }
        $("load-button").disabled = names.length === 0;
    } catch (error) {
        setStatus(`Failed to list animations: ${error.message}`, true);
    }
}

async function loadByName(name) {
    try {
        const response = await fetch(`${ANIMATIONS_URL}/${encodeURIComponent(name)}`);
        if (!response.ok) {
            throw new Error(await describeError(response));
        }
        loadAnimation(await response.json());
        setStatus(`Loaded "${name}"`);
    } catch (error) {
        setStatus(`Failed to load "${name}": ${error.message}`, true);
    }
}

$("load-button").addEventListener("click", () => {
    if (loadSelect.value) {
        loadByName(loadSelect.value);
    }
});

$("refresh-button").addEventListener("click", () => refreshList(loadSelect.value));

$("new-button").addEventListener("click", () => {
    loadAnimation({ Name: "", FrameRate: 10, Frames: [] });
    setStatus("New animation");
});

(async function init() {
    selectFrame(0);
    await refreshList();

    const requested = new URLSearchParams(location.search).get("name");
    if (requested) {
        await loadByName(requested);
    }
})();
