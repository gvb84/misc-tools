#!/usr/bin/env python

import os
import math
from Tkinter import *
from PIL import Image, ImageTk
import StringIO
import numpy as np
import cv2

W = 1280
H = 1024

def resize_image(image, size):
	w = image.size[0]/size[0]
	h = image.size[1]/size[1]
	ratio = max(w, h)
	if ratio == 0: 
		return image
	newsize = (int(image.size[0]/ratio),int(image.size[1]/ratio))
	return image.resize(newsize, Image.ANTIALIAS)

class App(Frame):
	def __init__(self, parent, files):
		Frame.__init__(self, parent)
		self.update()
		self.image_files = files
		self.current_image = 0
		imgdata = self.read_current_image()
		self.original = Image.open(imgdata)
		self.image_size = (math.floor(.7*W),math.floor(.7*H))
		resized = resize_image(self.original, self.image_size)
		self.image = ImageTk.PhotoImage(resized)
		self.display = Label(self, image=self.image, borderwidth="50")
		self.display.grid(row=0, column=0)
		self.pack(fill=BOTH, expand=1)

		cf = Frame(parent, borderwidth="50")
		cf.pack(side=LEFT)
		self.show_edge = IntVar()
		c = Checkbutton(cf, text="Show", variable=self.show_edge, bg="#ff0000")
		c.pack(side=TOP)
		l = Label(cf, text="Canny Edge detection")
		l.pack(side=TOP)
		self.minval = Scale(cf, from_=0, to=1000, orient=HORIZONTAL, label="minVal")
		self.minval.pack(side=LEFT)
		self.minval.set(50)
		self.maxval = Scale(cf, from_=0, to=1000, orient=HORIZONTAL, label="maxVal")
		self.maxval.set(150)
		self.maxval.pack(side=LEFT)
		self.asz = Scale(cf, from_=0, to=25, orient=HORIZONTAL, label="apertureSize")
		self.asz.set(3)
		self.asz.pack(side=LEFT)

		hf = Frame(parent, borderwidth="50")
		hf.pack(side=LEFT)
		self.show_hough = IntVar()
		c = Checkbutton(hf, text="Show", variable=self.show_hough, bg="#00ff00")
		c.pack()
		l = Label(hf, text="Hough Transformation")
		l.pack(side=TOP)
		self.mll = Scale(hf, from_=1, to=1000, orient=HORIZONTAL, label="minLineLength")
		self.mll.set(300)
		self.mll.pack(side=LEFT)
		self.mlg = Scale(hf, from_=1, to=1000, orient=HORIZONTAL, label="maxLineGap")
		self.mlg.set(5)
		self.mlg.pack(side=LEFT)
		self.t = Scale(hf, from_=1, to=1000, orient=HORIZONTAL, label="threshold")
		self.t.set(100)
		self.t.pack(side=LEFT)

		bf = Frame(parent, borderwidth="50")
		bf.pack(side=LEFT)
		b = Button(bf, text="Do it!", command=self.doit)
		b.pack(side=LEFT)

		bf = Frame(parent, borderwidth="50")
		bf.pack(side=LEFT)
		b = Button(bf, text="Prev", command=self.prev_image)
		b.pack(side=LEFT)
		b = Button(bf, text="Next", command=self.next_image)
		b.pack(side=LEFT)

		parent.bind('<Left>', self.prev_image)
		parent.bind('<Right>', self.next_image)
		parent.bind('<Return>', self.doit)
		parent.bind('<space>', self.doit)



		self.doit()

	def prev_image(self, event=None):
		self.current_image = self.current_image - 1
		if self.current_image < 0:
			self.current_image = len(self.image_files)-1
		self.doit()

	def next_image(self, event=None):
		self.current_image = self.current_image + 1
		if self.current_image == len(self.image_files):
			self.current_image = 0
		self.doit()

	def doit(self, event=None):
		fn = self.image_files[self.current_image]
		img = cv2.imread(fn, cv2.CV_LOAD_IMAGE_COLOR)
		edges = cv2.Canny(img, self.minval.get(), self.maxval.get(), self.asz.get())

		if edges is not None and self.show_edge.get() == 1:
			lx = len(edges[0])
			ly = len(edges)
			for y in range(0, ly):
				for x in range(0, lx):
					if (edges[y][x] == 255):
						cv2.line(img, (x,y),(x,y),(0,0,255),1)

		lines = cv2.HoughLinesP(edges, 1, np.pi/180, self.t.get(), self.mll.get(), self.mlg.get())

		if lines is not None and self.show_hough.get() == 1:
			for x1, y1, x2, y2 in lines[0]:
				cv2.line(img, (x1,y1),(x2,y2),(0,255,0),2)

		bla = cv2.imencode(".png", img)	
		s="".join(chr(x) for x in bla[1])
		self.original = Image.open(StringIO.StringIO(s))
		resized = resize_image(self.original, self.image_size)
		self.image = ImageTk.PhotoImage(resized)
		self.display.config(image=self.image)

	def read_current_image(self):
		fd = open(self.image_files[self.current_image], "r+b")
		ret = fd.read()
		fd.close()
		return StringIO.StringIO(ret)


def run():

	files = os.listdir(".")
	pngfiles = filter(lambda x:x.lower().endswith(".png"), files)
	imgdata = open("gmail.png", "r+b")
	bla=imgdata.read()
	imgdata = StringIO.StringIO(bla)

	root = Tk()
	root.minsize(width=W, height=H)
	root.geometry("%ix%i+0+0" % (W, H))
	app = App(root, pngfiles)
	app.mainloop()

if __name__ == "__main__":
	run()
