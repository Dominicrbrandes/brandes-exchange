/** @type {import('tailwindcss').Config} */
export default {
  content: ["./index.html", "./src/**/*.{js,ts,jsx,tsx}"],
  theme: {
    extend: {
      colors: {
        bg: { 1: '#0b0e11', 2: '#12161c', 3: '#1b2028', 4: '#242a33' },
        border: { DEFAULT: '#2b3139', light: '#3b4149' },
        text: { 1: '#eaecef', 2: '#848e9c', 3: '#5e6673' },
        long: { DEFAULT: '#02c076', dim: '#02c07620' },
        short: { DEFAULT: '#f6465d', dim: '#f6465d20' },
      },
      fontFamily: {
        sans: ['"IBM Plex Sans"', 'system-ui', 'sans-serif'],
        mono: ['"IBM Plex Mono"', 'monospace'],
      },
    },
  },
  plugins: [],
}
