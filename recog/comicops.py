#!/usr/bin/env python

from Tkinter import *
from PIL import ImageTk, Image
import ttk
import subprocess as sub
import urllib, thread


class ComiCops(Frame):
    def __init__(self, master = None):
        Frame.__init__(self, master)
        self.image = None
        self.pack(fill = "both", expand = "yes")
        self.createWidgets()
        
    def initWidgetLayout(self):
        # configure grids and make widgets resizable
        top = self.winfo_toplevel()
        top.rowconfigure(0, weight = 1)
        top.columnconfigure(0, weight = 1)
        self.rowconfigure(0, pad = 10)
        self.rowconfigure(1, weight = 1)
        self.rowconfigure(2, pad = 10)
        self.columnconfigure(0, weight = 1)
        
    def createWidgets(self):
        self.initWidgetLayout()
        self.uribox = Entry(self)
        self.uribox.bind("<Return>", self.on_enter_uri)
        self.uribox.grid(row = 0, column = 0, padx = 10, pady = 5, sticky = N+E+S+W)
        self.imagelabel = Label(self, bd = 2, relief = SUNKEN)
        self.imagelabel.grid(row = 1, column = 0, padx = 10, pady = 0, sticky = N+E+S+W)
        self.rating = Entry(self)
        self.rating.grid(row = 2, column = 0, padx = 10, pady = 5, sticky = N+E+S+W)
        self.bind("<Configure>", self.on_resize)


    def on_resize(self, event):
        if self.image is not None:
            self.update_image(event.width, event.height)

    def update_image(self, w_box, h_box):
        w, h = self.image.size
        f1, f2 = 1.0*w_box/w, 1.0*h_box/h  # 1.0 forces float division in Python2
        factor = min([f1, f2])
        width, height = int(w * factor), int(h * factor)
        im = self.image.resize((width, height), Image.ANTIALIAS)
        self.imagelabel.image = ImageTk.PhotoImage(im)
        self.imagelabel.config(image = self.imagelabel.image)

    def on_enter_uri(self, event):
        uri = self.uribox.get()
        if uri.startswith("file://"): uri = uri[7:]
        elif uri.startswith("http://"):
            urllib.urlretrieve(uri, "tmp")
            uri = "tmp"
            
        print "on_enter_uri:", uri
        self.image = Image.open(uri)
        self.update_image(
            self.imagelabel.winfo_width(),
            self.imagelabel.winfo_height())

        self.rating.delete(0, "end")
        self.rating.insert(0, "Rating ...")
        self.rating.config(bg = "#cccccc")
        thread.start_new_thread(lambda uri: self.rate_image(uri), (uri,))

    def rate_image(self, uri):
        # classify this image
        self.rating.delete(0, "end")
        p = sub.Popen(["sh", "./runrecog", uri], stdout=sub.PIPE, stderr=sub.PIPE)
        res = p.stdout.readline()
        if "safe" in res:
            self.rating.insert(0, "Safe")
            self.rating.config(bg = "#00ff00")
        elif "suspicious" in res:
            self.rating.insert(0, "Suspicious")
            self.rating.config(bg = "#ff0000")
        else:
            self.rating.insert(0, "Error: " + p.stderr.readline())
            self.rating.config(bg = "#777777")


root = Tk()
root.geometry("500x400")
root.title("ComiCops")
app = ComiCops(master = root)
root.config(width = 500, height = 500)
app.mainloop()
