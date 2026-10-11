const API_URL = "/API/lights";

const SEGMENTS = ["A", "B", "C", "D", "E", "F", "G"];
const COLORS = [
    { name: "red", code: "r" },
    { name: "yellow", code: "y" },
    { name: "green", code: "g" }
];

const digits = document.querySelectorAll(".digit");
const statusElement = document.getElementById("api-status");

// Create the 21 lights in each digit.
digits.forEach((digit, digitIndex) => {
    SEGMENTS.forEach((segmentName) => {
        const segment = document.createElement("div");
        segment.className = `segment ${segmentName.toLowerCase()}`;

        COLORS.forEach(({ name, code }) => {
            const light = document.createElement("button");

            light.type = "button";
            light.className = `light ${name}`;
            light.dataset.lightId =
                `${digitIndex}${segmentName}${code}`;

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

// Return a light's current state as 1 or 0.
function getLightBit(lightId) {
    const light = document.querySelector(
        `[data-light-id="${lightId}"]`
    );

    return light && light.classList.contains("on") ? 1 : 0;
}

/*
 * Build the complete bitstream in the required order:
 *
 * Digit 0: A, B, C, D, E, F, G
 * Digit 1: A, B, C, D, E, F, G
 * Colon:   X0, X1
 * Digit 2: A, B, C, D, E, F, G
 * Digit 3: A, B, C, D, E, F, G
 *
 * Each segment contains red, yellow, green.
 * Bits are packed LSB-first into bytes.
 */
function getOutputBytes() {
    const bits = [];

    function appendDigit(digitIndex) {
        for (const segment of SEGMENTS) {
            for (const { code } of COLORS) {
                const lightId = `${digitIndex}${segment}${code}`;
                bits.push(getLightBit(lightId));
            }
        }
    }

    appendDigit(0);
    appendDigit(1);

    // The colon contributes exactly two bits.
    bits.push(getLightBit("X0"));
    bits.push(getLightBit("X1"));

    appendDigit(2);
    appendDigit(3);

    // Pack bits LSB-first: the first bit becomes bit 0.
    const outputs = [];

    for (let i = 0; i < bits.length; i += 8) {
        let byte = 0;

        for (let bit = 0; bit < 8 && i + bit < bits.length; bit++) {
            byte |= bits[i + bit] << bit;
        }

        outputs.push(byte);
    }

    return outputs;
}

// Register click handlers on all digit and colon lights.
document.querySelectorAll(".light").forEach((light) => {
    light.addEventListener("click", () => {
        const isOn = light.classList.toggle("on");

        light.setAttribute("aria-pressed", String(isOn));

        sendLightStates();
    });
});

// Send the complete display state after every click.
async function sendLightStates() {
    const outputs = getOutputBytes();

    statusElement.textContent = "Sending...";

    try {
        const response = await fetch(API_URL, {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify({ outputs })
        });

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        statusElement.textContent = "Lights updated";
    } catch (error) {
        console.error("Failed to update lights:", error);
        statusElement.textContent = `API error: ${error.message}`;
    }
}