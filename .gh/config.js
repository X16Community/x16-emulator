const { marked } = require('marked');
const renderer = new marked.Renderer();

// Override function
renderer.link = (href, title, text) => {
    
    const myURL = new URL(href, "http://localhost/");
    if (myURL.hostname == "localhost") {
        href = myURL.hash;
    }

    return `
            <a href="${href}">
                ${text}
            </a>
            `
            
}

module.exports = {
    marked_options: { renderer },
};
