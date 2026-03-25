// main.js - JavaScript for Lua HTTP Server Demo

console.log('Lua HTTP Server Demo - JavaScript loaded');

// Helper: Show message to user
function showMessage(message, isError) {
    // Remove existing message if any
    const existingMsg = document.getElementById('form-message');
    if (existingMsg) {
        existingMsg.remove();
    }
    
    // Create message element
    const msgDiv = document.createElement('div');
    msgDiv.id = 'form-message';
    msgDiv.className = isError ? 'message error' : 'message success';
    msgDiv.innerHTML = message;
    
    // Insert before the form
    const form = document.getElementById('contact-form');
    if (form) {
        form.parentNode.insertBefore(msgDiv, form);
        
        // Auto-hide success message after 5 seconds
        if (!isError) {
            setTimeout(() => {
                msgDiv.style.opacity = '0';
                setTimeout(() => msgDiv.remove(), 300);
            }, 5000);
        }
    }
}

// Handle contact form submission
document.addEventListener('DOMContentLoaded', function() {
    const form = document.getElementById('contact-form');
    
    if (form) {
        form.addEventListener('submit', function(e) {
            e.preventDefault();
            
            // Get form data
            const formData = {
                name: document.getElementById('name').value.trim(),
                email: document.getElementById('email').value.trim(),
                message: document.getElementById('message').value.trim()
            };
            
            // Client-side validation
            if (!formData.name || !formData.email || !formData.message) {
                showMessage('Please fill in all fields.', true);
                return;
            }
            
            // Disable submit button while sending
            const submitBtn = form.querySelector('button[type="submit"]');
            const originalText = submitBtn.textContent;
            submitBtn.disabled = true;
            submitBtn.textContent = 'Sending...';
            
            // Send to server
            fetch('/api/contact', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(formData)
            })
            .then(response => response.json())
            .then(data => {
                // Show message from Lua script
                if (data.success) {
                    showMessage(data.message || 'Message sent successfully!', false);
                    form.reset();
                } else {
                    showMessage(data.error || 'Failed to send message.', true);
                }
            })
            .catch(error => {
                console.error('Error:', error);
                showMessage('Network error. Please try again later.', true);
            })
            .finally(() => {
                // Re-enable submit button
                submitBtn.disabled = false;
                submitBtn.textContent = originalText;
            });
        });
    }
});

