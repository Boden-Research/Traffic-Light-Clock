# Web Server

The web server allows users to interact with the clock using a web page. Web page will make HTTP requests to registered API endpoints for performing actions on the clock.

## /API/set

Sets the current output state of the lights.

This is a temporary API endpoint for testing/development.

### Data Format

```json
{
    "outputs": [ 1, 2, 4, 8, 16, 32, 64, 128 ]
}
```
